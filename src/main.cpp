/* drs_muones: adquisicion continua con la DRS4 Evaluation Board.
 *
 * Toda la configuracion esta en config.h. Manual: ../Docs/manual_drs_muones.pdf
 *
 *   ./drs_muones               -> mide hasta Ctrl+C / kill / corte de SSH
 *   ./drs_muones --selftest N  -> genera N triggers inventados, sin placa
 *
 * Como esta armado (dos hilos que trabajan en paralelo):
 *
 *   hilo principal:  espera trigger -> lee la placa -> filtro -> cola en RAM
 *   hilo escritor :  cola en RAM -> archivo .bin
 *
 * Asi una escritura lenta del disco no frena la medicion.
 */

#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "DRS.h"

#include "config.h"
#include "binformat.h"
#include "filtro.h"

/* ------------------------------------------------------------------------ */
/* Corte con Ctrl+C / kill / SIGHUP                                          */
/* ------------------------------------------------------------------------ */

static std::atomic<int>    g_stop{0};       /* 1 = terminar ordenadamente     */
static std::atomic<int>    g_force{0};      /* 1 = salir ya, sin vaciar cola  */
static std::atomic<long>   g_stop_at{0};

static void on_signal(int)
{
   if (!g_stop.load()) {
      g_stop.store(1);
      g_stop_at.store((long)time(nullptr));
      return;
   }
   /* un segundo corte, al menos 1 s despues, sale sin vaciar la cola */
   if (time(nullptr) - g_stop_at.load() >= 1)
      g_force.store(1);
}

/* ------------------------------------------------------------------------ */
/* Utilidades                                                                */
/* ------------------------------------------------------------------------ */

static uint64_t now_ns()
{
   using namespace std::chrono;
   return (uint64_t)duration_cast<nanoseconds>(
              system_clock::now().time_since_epoch()).count();
}

static std::string ahora(const char *formato)
{
   char buf[64];
   time_t t = time(nullptr);
   struct tm tm;
   localtime_r(&t, &tm);
   strftime(buf, sizeof buf, formato, &tm);
   return buf;
}

/* Crea la carpeta (y las intermedias) si no existen. */
static bool crear_carpeta(const std::string &path)
{
   std::string p;
   for (size_t i = 0; i < path.size(); i++) {
      p += path[i];
      if (path[i] == '/' || i + 1 == path.size()) {
         std::string d = p;
         while (d.size() > 1 && d.back() == '/')
            d.pop_back();
         if (d.empty() || d == "." || d == "/")
            continue;
         if (mkdir(d.c_str(), 0775) != 0 && errno != EEXIST) {
            perror(("ERROR: no se pudo crear la carpeta " + d).c_str());
            return false;
         }
      }
   }
   return true;
}

/* Une carpeta y nombre de archivo poniendo la barra que haga falta.
 * (Antes se pegaban sin barra: con "ang_0" los archivos quedaban afuera de
 * la carpeta, con nombre "ang_0run_....bin".) */
static std::string unir_ruta(const std::string &carpeta, const std::string &nombre)
{
   if (carpeta.empty())
      return nombre;
   if (carpeta.back() == '/')
      return carpeta + nombre;
   return carpeta + "/" + nombre;
}

static const char *nombre_modo(int modo)
{
   return modo == REQUERIDO ? "REQUERIDO" : modo == VETO ? "VETO" : "IGNORAR";
}

static const int   kModo[kNumChannels]   = {FILTRO_CH1, FILTRO_CH2, FILTRO_CH3, FILTRO_CH4};
static const float kUmbral[kNumChannels] = {(float)(UMBRAL_CH1_MV), (float)(UMBRAL_CH2_MV),
                                            (float)(UMBRAL_CH3_MV), (float)(UMBRAL_CH4_MV)};

/* ------------------------------------------------------------------------ */
/* Posicion del pulso dentro de la ventana                                   */
/* ------------------------------------------------------------------------ */

/* La placa frena el muestreo un rato DESPUES del trigger. Ese rato es un
 * retardo fijo mas una cantidad de "ticks" (0..255) que se puede elegir.
 * Como la ventana son los ultimos 1024 bins antes de frenar, el pulso queda
 *
 *     posicion = ventana - retardo_fijo - ticks * NS_POR_TICK
 *
 * contado desde el comienzo de la ventana. Aca se despeja ticks para que el
 * pulso quede donde pide POSICION_PULSO_NS. */
struct PlanTrigger {
   double ventana_ns;     /* duracion total de la ventana                  */
   double fijo_ns;        /* retardo fijo de la placa                      */
   int    ticks;          /* lo que se le programa a la placa (0..255)     */
   double posicion_ns;    /* donde va a quedar el pulso realmente          */
   double pos_min_ns;     /* rango alcanzable                              */
   double pos_max_ns;
   bool   recortado;      /* lo pedido no se podia alcanzar                */
};

static PlanTrigger planear_trigger(double f_ghz, double pedido_ns)
{
   PlanTrigger p;
   p.ventana_ns = kNumSamples / f_ghz;
   p.fijo_ns = 23.5 + 28.2 / f_ghz;   /* misma formula que usa la libreria */
   p.pos_max_ns = p.ventana_ns - p.fijo_ns;
   p.pos_min_ns = p.pos_max_ns - 255 * (double)(NS_POR_TICK);
   if (p.pos_min_ns < 0)
      p.pos_min_ns = 0;

   double ticks = (p.ventana_ns - p.fijo_ns - pedido_ns) / (double)(NS_POR_TICK);
   long t = lround(ticks);
   p.recortado = (t < 0 || t > 255);
   if (t < 0)
      t = 0;
   if (t > 255)
      t = 255;
   p.ticks = (int)t;
   p.posicion_ns = p.ventana_ns - p.fijo_ns - p.ticks * (double)(NS_POR_TICK);
   return p;
}

/* ------------------------------------------------------------------------ */
/* Cola en RAM entre la medicion y el disco                                  */
/* ------------------------------------------------------------------------ */

class Cola {
public:
   explicit Cola(size_t n) : ring_(n) {}

   /* Agrega un evento. Si la cola esta llena espera a que haya lugar (no se
    * descarta nada). Devuelve false solo si se pidio cortar mientras esperaba. */
   bool push(const Event &ev)
   {
      std::unique_lock<std::mutex> lk(m_);
      while (count_ == ring_.size() && !g_stop.load())
         hay_lugar_.wait_for(lk, std::chrono::milliseconds(100));
      if (count_ == ring_.size())
         return false;
      ring_[tail_] = ev;
      tail_ = (tail_ + 1) % ring_.size();
      count_++;
      hay_datos_.notify_one();
      return true;
   }

   /* Saca un evento. Devuelve false cuando la cola esta vacia y ya no va a
    * llegar nada mas (se llamo a cerrar()). */
   bool pop(Event &ev)
   {
      std::unique_lock<std::mutex> lk(m_);
      while (count_ == 0 && !cerrada_)
         hay_datos_.wait_for(lk, std::chrono::milliseconds(100));
      if (count_ == 0)
         return false;
      ev = ring_[head_];
      head_ = (head_ + 1) % ring_.size();
      count_--;
      hay_lugar_.notify_one();
      return true;
   }

   void cerrar()
   {
      std::lock_guard<std::mutex> lk(m_);
      cerrada_ = true;
      hay_datos_.notify_all();
   }

   size_t ocupados()
   {
      std::lock_guard<std::mutex> lk(m_);
      return count_;
   }

private:
   std::mutex              m_;
   std::condition_variable hay_lugar_;
   std::condition_variable hay_datos_;
   std::vector<Event>      ring_;
   size_t                  head_ = 0;
   size_t                  tail_ = 0;
   size_t                  count_ = 0;
   bool                    cerrada_ = false;
};

/* ------------------------------------------------------------------------ */
/* Hilo escritor                                                             */
/* ------------------------------------------------------------------------ */

static std::atomic<uint64_t> g_escritos{0};
static std::atomic<int>      g_archivo_idx{0};
static std::atomic<bool>     g_error_escritura{false};
static std::atomic<bool>     g_escritor_termino{false};

struct Escritor {
   std::string carpeta;
   std::string run_id;
   std::string comentario;
   double      fs_hz = 0;
   uint16_t    serial = 0;
   uint16_t    fw = 0;
   uint32_t    flags = 0;
   FILE       *f = nullptr;
   int         idx = 0;
   std::chrono::steady_clock::time_point abierto;

   bool abrir_nuevo()
   {
      std::string p = unir_ruta(carpeta, run_id + "_idx" + std::to_string(idx) + ".bin");
      f = fopen(p.c_str(), "wb");
      if (!f) {
         perror(("ERROR: no se pudo abrir " + p).c_str());
         return false;
      }
      /* buffer grande: el disco recibe bloques de 1 MB en vez de pedacitos */
      setvbuf(f, nullptr, _IOFBF, 1 << 20);

      FileHeader h;
      memset(&h, 0, sizeof h);
      memcpy(h.magic, kBinMagic, 8);
      h.version = kBinVersion;
      h.n_channels = kNumChannels;
      h.n_samples = kNumSamples;
      h.sampling_hz = fs_hz;
      h.board_serial = serial;
      h.fw_version = fw;
      h.flags = flags;
      h.start_unix_ns = now_ns();
      snprintf(h.comment, sizeof h.comment, "%s", comentario.c_str());

      if (fwrite(&h, 1, sizeof h, f) != sizeof h) {
         perror("ERROR: escribiendo el encabezado");
         fclose(f);
         f = nullptr;
         return false;
      }

      abierto = std::chrono::steady_clock::now();
      g_archivo_idx.store(idx);
      idx++;
      return true;
   }

   bool cerrar()
   {
      if (!f)
         return true;
      bool ok = (fflush(f) == 0);
#if FORZAR_ESCRITURA_A_DISCO
      fsync(fileno(f));
#endif
      if (fclose(f) != 0)
         ok = false;
      f = nullptr;
      if (!ok)
         perror("ERROR: cerrando el archivo");
      return ok;
   }

   bool toca_rotar() const
   {
      auto seg = std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::steady_clock::now() - abierto).count();
      return seg >= (long long)(MINUTOS_POR_ARCHIVO) * 60;
   }

   bool escribir(const Event &ev)
   {
      if (!f && !abrir_nuevo())
         return false;
      if (toca_rotar() && !(cerrar() && abrir_nuevo()))
         return false;
      if (fwrite(&ev, 1, sizeof ev, f) != sizeof ev) {
         perror("ERROR: escribiendo un evento");
         return false;
      }
      return true;
   }
};

static void hilo_escritor(Escritor *w, Cola *cola)
{
   /* static: 20 KB que no hace falta poner en la pila del hilo */
   static Event ev;
   auto ultimo_flush = std::chrono::steady_clock::now();

   while (cola->pop(ev)) {
      if (g_error_escritura.load())
         continue;   /* ya fallo: se sigue vaciando la cola para no trabar */

      if (!w->escribir(ev)) {
         fprintf(stderr, "ERROR: no se pudo escribir (¿disco lleno?). "
                         "Se detiene la medicion.\n");
         g_error_escritura.store(true);
         g_stop.store(1);
         continue;
      }
      g_escritos++;

      /* que lo escrito no quede mas de 2 s solo en memoria del programa */
      auto t = std::chrono::steady_clock::now();
      if (t - ultimo_flush > std::chrono::seconds(2)) {
         fflush(w->f);
         ultimo_flush = t;
      }
   }

   if (!w->cerrar())
      g_error_escritura.store(true);
   g_escritor_termino.store(true);
}

/* ------------------------------------------------------------------------ */
/* Eventos inventados para --selftest (sin placa)                            */
/* ------------------------------------------------------------------------ */

/* Suma al canal un pulso que empieza en t0 y llega hasta "pico" mV. */
static void sumar_pulso(float *v, const float *t, double t0, double pico)
{
   const double forma_max = 0.715;   /* maximo de la forma de abajo */
   for (int i = 0; i < kNumSamples; i++) {
      double x = t[i] - t0;
      if (x <= 0)
         continue;
      v[i] += (float)(pico / forma_max * exp(-x / 15.0) * (1.0 - exp(-x / 1.5)));
   }
}

/* Genera un evento parecido a uno real: CH1 siempre tiene pulso (es el que
 * dispara); CH2 lo tiene en coincidencia el 60 % de las veces y muy tarde el
 * 10 %; CH3 el 50 %; CH4 el 5 %. Sirve para probar el filtro sin placa. */
static void evento_sintetico(std::mt19937 &rng, double f_ghz, double pos_ns,
                             float *t, float onda[kNumChannels][kNumSamples])
{
   std::normal_distribution<double> ruido(0.0, 4.0), jitter(0.0, 1.0);
   std::uniform_real_distribution<double> azar(0.0, 1.0);

   for (int i = 0; i < kNumSamples; i++) {
      t[i] = (float)(i / f_ghz);
      for (int c = 0; c < kNumChannels; c++)
         onda[c][i] = (float)ruido(rng);
   }

   double ventana = kNumSamples / f_ghz;
   double t1 = pos_ns + jitter(rng);
   double lejos = t1 + 0.5 * (ventana - t1);   /* un pulso "tardio" */

   sumar_pulso(onda[0], t, t1, 1.6 * kUmbral[0]);

   double u = azar(rng);
   if (u < 0.60)
      sumar_pulso(onda[1], t, t1 + 2.0 + jitter(rng), 1.6 * kUmbral[1]);
   else if (u < 0.70)
      sumar_pulso(onda[1], t, lejos, 1.6 * kUmbral[1]);

   if (azar(rng) < 0.50)
      sumar_pulso(onda[2], t, t1 + 4.0 + jitter(rng), 1.6 * kUmbral[2]);
   if (azar(rng) < 0.05)
      sumar_pulso(onda[3], t, t1 + 6.0 + jitter(rng), 1.6 * kUmbral[3]);
}

/* ------------------------------------------------------------------------ */
/* Texto con la configuracion (va a pantalla y al archivo _info.txt)         */
/* ------------------------------------------------------------------------ */

static void imprimir_configuracion(FILE *out, const PlanTrigger &plan, double f_real,
                                   const std::string &carpeta_abs)
{
   fprintf(out, "Configuracion (se cambia en src/config.h):\n");
   fprintf(out, "  carpeta de salida : %s\n", carpeta_abs.c_str());
   fprintf(out, "  archivo nuevo cada: %d min\n", (int)(MINUTOS_POR_ARCHIVO));
   fprintf(out, "  frecuencia        : %.4f GHz (pedida %.3f) -> ventana de %.0f ns, "
                "una muestra cada %.2f ns\n",
           f_real, (double)(FRECUENCIA_GHZ), plan.ventana_ns, 1.0 / f_real);
   fprintf(out, "  trigger           : CH1, umbral %.0f mV, pulsos %s\n",
           (double)(UMBRAL_TRIGGER_MV), (PULSOS_NEGATIVOS) ? "negativos" : "positivos");
   fprintf(out, "  posicion del pulso: %.0f ns desde el comienzo de la ventana "
                "(pedida %.0f; alcanzable %.0f a %.0f; %d ticks)\n",
           plan.posicion_ns, (double)(POSICION_PULSO_NS), plan.pos_min_ns,
           plan.pos_max_ns, plan.ticks);
   if (FILTRO_ACTIVADO) {
      fprintf(out, "  filtro            : ACTIVADO, ventana de coincidencia %.1f ns\n",
              (double)(VENTANA_COINCIDENCIA_NS));
      for (int c = 0; c < kNumChannels; c++) {
         if (kModo[c] == IGNORAR)
            fprintf(out, "      CH%d: IGNORAR\n", c + 1);
         else
            fprintf(out, "      CH%d: %-9s (pulso = cruza %.0f mV)\n", c + 1,
                    nombre_modo(kModo[c]), (double)kUmbral[c]);
      }
   } else {
      fprintf(out, "  filtro            : desactivado (se guarda todo lo que dispara CH1)\n");
   }
   fprintf(out, "  cola en RAM       : %d eventos (%.0f MB)\n", (int)(EVENTOS_EN_COLA),
           (EVENTOS_EN_COLA) * (double)kBytesPerEvent / 1e6);
}

/* ------------------------------------------------------------------------ */
/* Programa principal                                                        */
/* ------------------------------------------------------------------------ */

int main(int argc, char **argv)
{
   long selftest = 0;

   for (int i = 1; i < argc; i++) {
      if (!strcmp(argv[i], "--selftest")) {
         if (i + 1 >= argc) {
            printf("falta N despues de --selftest (ejemplo: --selftest 100)\n");
            return 2;
         }
         selftest = atol(argv[++i]);
         if (selftest <= 0) {
            printf("N debe ser mayor que cero\n");
            return 2;
         }
      } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
         printf("uso: %s [--selftest N]\n", argv[0]);
         printf("  sin argumentos : mide hasta Ctrl+C, kill o corte de SSH\n");
         printf("  --selftest N   : genera N triggers inventados, sin placa\n");
         printf("La configuracion se cambia en src/config.h\n");
         return 0;
      } else {
         printf("argumento desconocido: %s (ver --help)\n", argv[i]);
         return 2;
      }
   }

   signal(SIGINT, on_signal);
   signal(SIGTERM, on_signal);
   signal(SIGHUP, on_signal);

   /* ---- placa ---- */

   DRS *drs = nullptr;
   DRSBoard *b = nullptr;
   double f_real = (double)(FRECUENCIA_GHZ);
   uint16_t serial = 0, fw = 0;
   bool tiempo_calibrado = false;

   if (selftest <= 0) {
      drs = new DRS();

      if (drs->GetNumberOfBoards() == 0) {
         printf("\nNo se encontro ninguna placa DRS4.\n");
         printf("  - Revisar el cable USB y que la placa tenga alimentacion.\n");
         printf("  - Si con 'sudo' anda y sin 'sudo' no, faltan los permisos de USB\n");
         printf("    (ver el manual, seccion de instalacion).\n");
         printf("Para probar sin placa: %s --selftest 100\n", argv[0]);
         delete drs;
         return 1;
      }

      for (int i = 0; i < drs->GetNumberOfBoards(); i++) {
         DRSBoard *bb = drs->GetBoard(i);
         printf("Placa DRS4 serie #%d, firmware %d\n",
                bb->GetBoardSerialNumber(), bb->GetFirmwareVersion());
      }

      b = drs->GetBoard(0);
      b->Init();
      if (!b->SetFrequency(FRECUENCIA_GHZ, true)) {
         printf("ERROR: la placa no acepto la frecuencia de %.3f GHz\n",
                (double)(FRECUENCIA_GHZ));
         delete drs;
         return 1;
      }
      b->SetTranspMode(1);
      b->SetInputRange(0);       /* entrada de -0.5 V a +0.5 V */
      b->EnableTcal(0);

      /* trigger por hardware en CH1 */
      if (b->GetBoardType() >= 8) {
         b->EnableTrigger(1, 0);
         b->SetTriggerConfig(1 << 0);
      } else if (b->GetBoardType() == 7) {
         b->EnableTrigger(0, 1);
         b->SetTriggerConfig(1);
      }
      b->SetTriggerLevel((UMBRAL_TRIGGER_MV) / 1000.0);
      b->SetTriggerPolarity((PULSOS_NEGATIVOS) ? true : false);

      f_real = b->GetNominalFrequency();
      serial = (uint16_t)b->GetBoardSerialNumber();
      fw = (uint16_t)b->GetFirmwareVersion();
      tiempo_calibrado = b->IsTimingCalibrationValid();
   } else {
      printf("MODO SELFTEST: %ld triggers inventados, sin placa\n", selftest);
   }

   /* ---- posicion del pulso ---- */

   PlanTrigger plan = planear_trigger(f_real, (double)(POSICION_PULSO_NS));

   if ((double)(POSICION_PULSO_NS) >= plan.ventana_ns) {
      printf("\nERROR: POSICION_PULSO_NS = %.0f ns no entra en la ventana, que a "
             "%.3f GHz dura %.0f ns.\n", (double)(POSICION_PULSO_NS), f_real,
             plan.ventana_ns);
      printf("       El pulso quedaria afuera de lo que se guarda. Elegir en config.h "
             "un valor entre %.0f y %.0f.\n", plan.pos_min_ns, plan.pos_max_ns);
      delete drs;
      return 1;
   }

   if (b) {
      /* La libreria pide el retardo en ns y lo pasa a ticks dividiendo por su
       * propia constante; se le pide el valor que da justo los ticks buscados. */
      int tipo = b->GetBoardType();
      double ns_tick_lib;
      if (tipo == 7 || tipo == 8 || tipo == 9)
         ns_tick_lib = (b->GetFirmwareVersion() >= 17147) ? 6.2 : 2.1;
      else
         ns_tick_lib = (b->GetFirmwareVersion() >= 17382) ? 4.6 : 2.3;
      b->SetTriggerDelayNs((int)lround(plan.ticks * ns_tick_lib));
   }

   /* ---- carpeta de salida ---- */

   std::string carpeta = CARPETA_SALIDA;
   if (!crear_carpeta(carpeta)) {
      printf("No se pudo crear la carpeta de salida %s\n", carpeta.c_str());
      delete drs;
      return 1;
   }
   std::string carpeta_abs = carpeta;
   {
      char buf[PATH_MAX];
      if (realpath(carpeta.c_str(), buf))
         carpeta_abs = buf;
   }

   printf("\n");
   imprimir_configuracion(stdout, plan, f_real, carpeta_abs);

   if (plan.recortado)
      printf("\nAVISO: no se puede poner el pulso en %.0f ns. A esta frecuencia la placa\n"
             "       solo llega de %.0f a %.0f ns. Se usa %.0f ns.\n",
             (double)(POSICION_PULSO_NS), plan.pos_min_ns, plan.pos_max_ns,
             plan.posicion_ns);
   if (b && !tiempo_calibrado)
      printf("\nAVISO: la placa no tiene calibracion temporal para esta frecuencia\n"
             "       (esta calibrada a %.3f GHz). Los tiempos guardados suponen que\n"
             "       todas las muestras duran lo mismo; el error puede llegar a ~10 ns.\n"
             "       Ver scripts/correccion_temporal.html\n",
             b->GetCalibratedFrequency());
   if ((FILTRO_ACTIVADO) && kModo[0] != REQUERIDO)
      printf("\nAVISO: el filtro esta activado pero CH1 no es REQUERIDO. CH1 es el canal\n"
             "       que dispara, asi que casi siempre tiene pulso: revisar FILTRO_CH1.\n");
   printf("\n");
   fflush(stdout);

   /* ---- archivos ---- */

   std::string run_id = ahora("run_%Y%m%d_%H%M%S");

   char comentario[64];
   {
      char filt[24] = "no";
      if (FILTRO_ACTIVADO) {
         const char letra[3] = {'I', 'R', 'V'};
         snprintf(filt, sizeof filt, "%c%c%c%c/%.0fns", letra[kModo[0]], letra[kModo[1]],
                  letra[kModo[2]], letra[kModo[3]], (double)(VENTANA_COINCIDENCIA_NS));
      }
      snprintf(comentario, sizeof comentario,
               "thr=%.0fmV pos=%.0fns freq=%.3fGHz negEdge=%d filt=%s",
               (double)(UMBRAL_TRIGGER_MV), plan.posicion_ns, (double)(FRECUENCIA_GHZ),
               (int)(PULSOS_NEGATIVOS), filt);
   }

   Escritor w;
   w.carpeta = carpeta;
   w.run_id = run_id;
   w.comentario = comentario;
   w.fs_hz = f_real * 1e9;
   w.serial = serial;
   w.fw = fw;
   w.flags = (tiempo_calibrado ? kFileTiempoCalibrado : 0) |
             ((FILTRO_ACTIVADO) ? kFileFiltroActivado : 0);

   if (!w.abrir_nuevo()) {
      delete drs;
      return 1;
   }

   /* archivo de texto con la configuracion y, al final, el resumen */
   std::string info_path = unir_ruta(carpeta, run_id + "_info.txt");
   {
      FILE *fi = fopen(info_path.c_str(), "w");
      if (fi) {
         fprintf(fi, "Corrida %s  (empezo el %s)\n", run_id.c_str(),
                 ahora("%d/%m/%Y a las %H:%M:%S").c_str());
         if (b)
            fprintf(fi, "Placa DRS4 serie #%d, firmware %d, calibracion temporal %s\n\n",
                    (int)serial, (int)fw, tiempo_calibrado ? "valida" : "NO valida");
         else
            fprintf(fi, "MODO SELFTEST: datos inventados, sin placa\n\n");
         imprimir_configuracion(fi, plan, f_real, carpeta_abs);
         fclose(fi);
      }
   }

   printf("Midiendo. Para terminar: Ctrl+C (una sola vez; espera a que guarde todo).\n\n");
   fflush(stdout);

   static Cola cola(EVENTOS_EN_COLA);
   std::thread escritor(hilo_escritor, &w, &cola);

   /* ---- medicion ---- */

   static float tbuf[kNumSamples];
   static float wbuf[kNumChannels][kNumSamples];
   static Event ev;

   uint64_t n_trigger = 0;                   /* triggers leidos de la placa  */
   uint64_t n_decision[N_DECISIONES] = {0};  /* cuantos de cada tipo         */
   uint64_t n_saturados = 0;

   auto t_inicio = std::chrono::steady_clock::now();
   auto t_estado = t_inicio;
   uint64_t trig_estado = 0;

   auto imprimir_estado = [&](bool forzar) {
      auto t = std::chrono::steady_clock::now();
      double dt = std::chrono::duration<double>(t - t_estado).count();
      if (!forzar && dt < (double)(SEGUNDOS_ENTRE_ESTADOS))
         return;
      printf("[%s] triggers %llu (%.2f por seg) | guardados %llu | descartados %llu | "
             "cola %zu/%d | %s_idx%d.bin\n",
             ahora("%H:%M:%S").c_str(), (unsigned long long)n_trigger,
             dt > 0 ? (n_trigger - trig_estado) / dt : 0.0,
             (unsigned long long)n_decision[GUARDAR],
             (unsigned long long)(n_trigger - n_decision[GUARDAR]),
             cola.ocupados(), (int)(EVENTOS_EN_COLA), run_id.c_str(),
             g_archivo_idx.load());
      fflush(stdout);
      t_estado = t;
      trig_estado = n_trigger;
   };

   std::mt19937 rng(12345);   /* semilla fija: el selftest da siempre lo mismo */

   while (!g_stop.load()) {
      if (selftest > 0 && n_trigger >= (uint64_t)selftest)
         break;

      uint64_t t_trigger;
      int celda = 0;

      if (b) {
         /* 1) armar la placa y esperar el trigger */
         b->StartDomino();
         while (b->IsBusy() && !g_stop.load()) {
            usleep(ESPERA_SONDEO_US);
            imprimir_estado(false);
         }
         if (g_stop.load()) {
            b->Reinit();
            break;
         }
         t_trigger = now_ns();

         /* 2) traer las ondas a la computadora */
         b->TransferWaves(0, 6);   /* canales DRS 0..6 = CH1..CH4 */
         celda = b->GetTriggerCell(0);
         b->GetTime(0, 0, celda, tbuf);
         for (int c = 0; c < kNumChannels; c++)
            b->GetWave(0, 2 * c, wbuf[c]);
      } else {
         evento_sintetico(rng, f_real, plan.posicion_ns, tbuf, wbuf);
         t_trigger = now_ns();
      }

      /* 3) filtro: ¿se guarda? */
      ResultadoFiltro r = evaluar_filtro(tbuf, wbuf);
      n_decision[r.decision]++;

#if IMPRIMIR_CADA_EVENTO
      printf("trigger #%llu: %s |", (unsigned long long)n_trigger,
             kNombreDecision[r.decision]);
      for (int c = 0; c < kNumChannels; c++) {
         if (r.hay_pulso[c])
            printf(" CH%d en %.1f ns", c + 1, r.t_pulso[c]);
         else
            printf(" CH%d sin pulso", c + 1);
      }
      printf("\n");
      fflush(stdout);
#endif

      /* 4) si pasa el filtro, a la cola (el otro hilo lo escribe al disco) */
      if (r.decision == GUARDAR) {
         bool saturado = false;
         for (int i = 0; i < kNumSamples; i++) {
            ev.s[i][0] = tbuf[i];
            for (int c = 0; c < kNumChannels; c++) {
               float v = wbuf[c][i];
               ev.s[i][1 + c] = v;
               if (fabsf(v) > (float)(SATURACION_MV))
                  saturado = true;
            }
         }
         uint16_t flags = saturado ? kEvSaturado : 0;
         for (int c = 0; c < kNumChannels; c++)
            if (r.hay_pulso[c])
               flags |= (uint16_t)(kEvPulsoCh1 << c);
         if (saturado)
            n_saturados++;

         ev.hdr.unix_ns = t_trigger;
         ev.hdr.event_index = (uint32_t)n_trigger;
         ev.hdr.trigger_cell = (uint16_t)celda;
         ev.hdr.flags = flags;

         if (!cola.push(ev)) {
            n_decision[GUARDAR]--;   /* se corto con la cola llena: no entro */
            n_trigger++;
            break;
         }
      }

      n_trigger++;
      imprimir_estado(false);
   }

   /* ---- cierre ---- */

   double duracion = std::chrono::duration<double>(
                         std::chrono::steady_clock::now() - t_inicio).count();

   if (b)
      b->Reinit();

   if (g_stop.load())
      printf("\nCorte pedido: guardando lo que quedaba en la cola...\n");
   else
      printf("\nFin de la corrida: guardando lo que quedaba en la cola...\n");
   fflush(stdout);

   cola.cerrar();
   while (!g_escritor_termino.load()) {
      if (g_force.load()) {
         printf("Segundo corte: salida inmediata (pueden faltar eventos en el archivo)\n");
         fflush(stdout);
         _exit(1);
      }
      usleep(20000);
   }
   escritor.join();

   delete drs;

   uint64_t guardados = n_decision[GUARDAR];
   uint64_t escritos = g_escritos.load();
   bool ok = !g_error_escritura.load() && escritos == guardados;

   FILE *salidas[2] = {stdout, fopen(info_path.c_str(), "a")};
   for (FILE *out : salidas) {
      if (!out)
         continue;
      fprintf(out, "\nResumen de la corrida %s:\n", run_id.c_str());
      fprintf(out, "  duracion             : %.1f s (%.2f horas)\n", duracion,
              duracion / 3600.0);
      fprintf(out, "  triggers de CH1      : %llu (%.3f por seg)\n",
              (unsigned long long)n_trigger, duracion > 0 ? n_trigger / duracion : 0.0);
      fprintf(out, "  eventos guardados    : %llu (%.3f por seg)\n",
              (unsigned long long)guardados, duracion > 0 ? guardados / duracion : 0.0);
      if (FILTRO_ACTIVADO) {
         fprintf(out, "  descartados          : %llu\n",
                 (unsigned long long)(n_trigger - guardados));
         fprintf(out, "      falta un pulso REQUERIDO   : %llu\n",
                 (unsigned long long)n_decision[DESCARTE_FALTA_PULSO]);
         fprintf(out, "      fuera de la ventana        : %llu\n",
                 (unsigned long long)n_decision[DESCARTE_FUERA_VENTANA]);
         fprintf(out, "      pulso en un canal VETO     : %llu\n",
                 (unsigned long long)n_decision[DESCARTE_VETO]);
      }
      fprintf(out, "  guardados y saturados: %llu\n", (unsigned long long)n_saturados);
      fprintf(out, "  archivos .bin        : %d, en %s\n", w.idx, carpeta_abs.c_str());
      if (ok)
         fprintf(out, "  escritura            : OK, los %llu eventos estan en disco\n",
                 (unsigned long long)escritos);
      else
         fprintf(out, "  escritura            : ERROR, solo se escribieron %llu de %llu\n",
                 (unsigned long long)escritos, (unsigned long long)guardados);
      if (out != stdout)
         fclose(out);
   }

   return ok ? 0 : 1;
}

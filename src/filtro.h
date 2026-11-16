#ifndef DRS_MUONES_FILTRO_H
#define DRS_MUONES_FILTRO_H

/* Filtro de eventos: decide si un evento se guarda o se descarta.
 * Lo que pide el filtro se configura en config.h (seccion 4).
 * Explicacion: ../Docs/manual_drs_muones.pdf, seccion "Filtro de eventos". */

#include "binformat.h"
#include "config.h"

/* Opciones para FILTRO_CH1 ... FILTRO_CH4 */
#define IGNORAR    0
#define REQUERIDO  1
#define VETO       2

/* Muestras que no se miran en cada borde de la ventana: ahi el chip suele
 * dejar picos que no son señal. */
static const int kBordeIgnorado = 8;

enum Decision {
   GUARDAR = 0,
   DESCARTE_FALTA_PULSO,     /* un canal REQUERIDO no tuvo pulso                */
   DESCARTE_FUERA_VENTANA,   /* los pulsos REQUERIDO estan demasiado separados  */
   DESCARTE_VETO,            /* un canal VETO tuvo pulso                        */
   N_DECISIONES
};

static const char *const kNombreDecision[N_DECISIONES] = {
   "guardado",
   "descartado: falta el pulso en un canal REQUERIDO",
   "descartado: pulsos fuera de la ventana de coincidencia",
   "descartado: pulso en un canal VETO",
};

struct ResultadoFiltro {
   Decision decision;
   bool     hay_pulso[kNumChannels];   /* el canal cruzo su umbral            */
   float    t_pulso[kNumChannels];     /* en que tiempo lo cruzo (ns)         */
};

/* Busca la primera vez que el canal v cruza el umbral. Devuelve true si hay
 * cruce y deja en *t_cruce el tiempo, interpolando entre las dos muestras. */
static inline bool buscar_pulso(const float *v, const float *t, float umbral,
                                bool negativo, float *t_cruce)
{
   for (int i = kBordeIgnorado; i < kNumSamples - kBordeIgnorado; i++) {
      bool paso = negativo ? (v[i] <= umbral) : (v[i] >= umbral);
      if (!paso)
         continue;
      if (i == kBordeIgnorado || v[i] == v[i - 1]) {
         *t_cruce = t[i];
      } else {
         float frac = (umbral - v[i - 1]) / (v[i] - v[i - 1]);
         *t_cruce = t[i - 1] + frac * (t[i] - t[i - 1]);
      }
      return true;
   }
   return false;
}

/* Revisa un evento. t = tiempos (ns), onda[c] = canal c en mV. */
static inline ResultadoFiltro evaluar_filtro(const float *t,
                                             const float onda[kNumChannels][kNumSamples])
{
   static const int   modo[kNumChannels]   = {FILTRO_CH1, FILTRO_CH2, FILTRO_CH3, FILTRO_CH4};
   static const float umbral[kNumChannels] = {(float)(UMBRAL_CH1_MV), (float)(UMBRAL_CH2_MV),
                                              (float)(UMBRAL_CH3_MV), (float)(UMBRAL_CH4_MV)};
   ResultadoFiltro r;
   r.decision = GUARDAR;

   for (int c = 0; c < kNumChannels; c++) {
      r.t_pulso[c] = 0;
      r.hay_pulso[c] = buscar_pulso(onda[c], t, umbral[c], PULSOS_NEGATIVOS != 0, &r.t_pulso[c]);
   }

   if (!FILTRO_ACTIVADO)
      return r;

   /* 1) VETO: un pulso en cualquier lugar de la ventana descarta el evento */
   for (int c = 0; c < kNumChannels; c++)
      if (modo[c] == VETO && r.hay_pulso[c]) {
         r.decision = DESCARTE_VETO;
         return r;
      }

   /* 2) REQUERIDO: todos tienen que tener pulso */
   float primero = 0, ultimo = 0;
   bool alguno = false;
   for (int c = 0; c < kNumChannels; c++) {
      if (modo[c] != REQUERIDO)
         continue;
      if (!r.hay_pulso[c]) {
         r.decision = DESCARTE_FALTA_PULSO;
         return r;
      }
      if (!alguno || r.t_pulso[c] < primero)
         primero = r.t_pulso[c];
      if (!alguno || r.t_pulso[c] > ultimo)
         ultimo = r.t_pulso[c];
      alguno = true;
   }

   /* 3) ...y empezar cerca uno del otro */
   if (alguno && ultimo - primero > (float)(VENTANA_COINCIDENCIA_NS))
      r.decision = DESCARTE_FUERA_VENTANA;

   return r;
}

/* ---- Controles de config.h: mensajes claros si algun valor esta mal ---- */

#define MODO_VALIDO(m) ((m) == IGNORAR || (m) == REQUERIDO || (m) == VETO)

static_assert(MODO_VALIDO(FILTRO_CH1) && MODO_VALIDO(FILTRO_CH2) &&
              MODO_VALIDO(FILTRO_CH3) && MODO_VALIDO(FILTRO_CH4),
              "config.h: FILTRO_CH1..FILTRO_CH4 tienen que ser REQUERIDO, IGNORAR o VETO");
static_assert((FILTRO_ACTIVADO) == 0 || (FILTRO_ACTIVADO) == 1,
              "config.h: FILTRO_ACTIVADO tiene que ser 0 o 1");
static_assert((VENTANA_COINCIDENCIA_NS) >= 0,
              "config.h: VENTANA_COINCIDENCIA_NS no puede ser negativa");
static_assert((PULSOS_NEGATIVOS) == 0 || (PULSOS_NEGATIVOS) == 1,
              "config.h: PULSOS_NEGATIVOS tiene que ser 0 o 1");
static_assert((FRECUENCIA_GHZ) >= 0.1 && (FRECUENCIA_GHZ) <= 6.0,
              "config.h: FRECUENCIA_GHZ tiene que estar entre 0.1 y 6");
static_assert((UMBRAL_TRIGGER_MV) > -500 && (UMBRAL_TRIGGER_MV) < 500,
              "config.h: UMBRAL_TRIGGER_MV tiene que estar entre -500 y 500 mV");
static_assert((PULSOS_NEGATIVOS) ? (UMBRAL_TRIGGER_MV) < 0 : (UMBRAL_TRIGGER_MV) > 0,
              "config.h: UMBRAL_TRIGGER_MV y PULSOS_NEGATIVOS no son coherentes "
              "(pulsos negativos -> umbral negativo; pulsos positivos -> umbral positivo)");
#define UMBRAL_COHERENTE(u) ((PULSOS_NEGATIVOS) ? (u) < 0 : (u) > 0)
static_assert(UMBRAL_COHERENTE(UMBRAL_CH1_MV) && UMBRAL_COHERENTE(UMBRAL_CH2_MV) &&
              UMBRAL_COHERENTE(UMBRAL_CH3_MV) && UMBRAL_COHERENTE(UMBRAL_CH4_MV),
              "config.h: UMBRAL_CH1_MV..UMBRAL_CH4_MV tienen que tener el mismo signo que "
              "los pulsos (pulsos negativos -> umbrales negativos)");
static_assert((POSICION_PULSO_NS) >= 0,
              "config.h: POSICION_PULSO_NS no puede ser negativa");
static_assert((MINUTOS_POR_ARCHIVO) > 0,
              "config.h: MINUTOS_POR_ARCHIVO tiene que ser mayor que cero");
static_assert((EVENTOS_EN_COLA) >= 2,
              "config.h: EVENTOS_EN_COLA tiene que ser al menos 2");
static_assert((SEGUNDOS_ENTRE_ESTADOS) > 0,
              "config.h: SEGUNDOS_ENTRE_ESTADOS tiene que ser mayor que cero");
static_assert((ESPERA_SONDEO_US) >= 0 && (ESPERA_SONDEO_US) <= 100000,
              "config.h: ESPERA_SONDEO_US tiene que estar entre 0 y 100000");
static_assert((NS_POR_TICK) > 0,
              "config.h: NS_POR_TICK tiene que ser mayor que cero");
static_assert(sizeof(CARPETA_SALIDA) > 1,
              "config.h: CARPETA_SALIDA no puede estar vacia");

#endif

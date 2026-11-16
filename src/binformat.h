#ifndef DRS_MUONES_BINFORMAT_H
#define DRS_MUONES_BINFORMAT_H

#include <cstddef>
#include <cstdint>

/* Formato de los archivos .bin (little-endian).
 * Explicado paso a paso en ../Docs/manual_drs_muones.pdf y en scripts/LEEME.md
 *
 * Version 2: el tamaño y el orden de los campos son los mismos que en la
 * version 1, asi que los scripts que leian la version 1 siguen funcionando.
 * Lo unico nuevo es el significado de algunos bits que antes valian cero. */

static const int      kNumChannels   = 4;      /* CH1..CH4 (canales DRS 0,2,4,6) */
static const int      kNumSamples    = 1024;   /* muestras por evento            */
static const uint16_t kBinVersion    = 2;
static const char     kBinMagic[8]   = {'M','U','O','N','B','I','N','1'};

/* Bits de FileHeader.flags */
static const uint32_t kFileTiempoCalibrado = 1u << 0;  /* t usa la calibracion de la placa */
static const uint32_t kFileFiltroActivado  = 1u << 1;  /* se midio con FILTRO_ACTIVADO = 1 */

/* Bits de EventHeader.flags */
static const uint16_t kEvSaturado = 1u << 0;           /* algun canal paso SATURACION_MV  */
static const uint16_t kEvPulsoCh1 = 1u << 1;           /* CH1 cruzo su umbral             */
/* bit 2 = pulso en CH2, bit 3 = pulso en CH3, bit 4 = pulso en CH4 */

/* Encabezado del archivo: 128 bytes. */
struct FileHeader {
   char     magic[8];        /* "MUONBIN1"                                  */
   uint16_t version;         /* 2                                           */
   uint16_t n_channels;      /* 4                                           */
   uint32_t n_samples;       /* 1024                                        */
   double   sampling_hz;     /* frecuencia real de muestreo, en Hz          */
   uint16_t board_serial;    /* numero de serie de la placa (0 en selftest) */
   uint16_t fw_version;      /* firmware de la placa (0 en selftest)        */
   uint32_t flags;           /* ver kFile...                                */
   uint64_t start_unix_ns;   /* cuando se abrio el archivo                  */
   char     comment[64];     /* configuracion resumida, en texto            */
   uint8_t  reserved[24];
};

/* Encabezado de cada evento: 16 bytes. */
struct EventHeader {
   uint64_t unix_ns;         /* fecha y hora del trigger (ns desde 1970)    */
   uint32_t event_index;     /* numero de trigger dentro de la corrida      */
   uint16_t trigger_cell;    /* celda del chip donde freno el muestreo      */
   uint16_t flags;           /* ver kEv...                                  */
};

/* Un evento completo: encabezado + 1024 filas de {t[ns], ch1..ch4[mV]}. */
struct Event {
   EventHeader hdr;
   float       s[kNumSamples][1 + kNumChannels];
};

static_assert(sizeof(FileHeader) == 128, "FileHeader debe medir 128 bytes");
static_assert(sizeof(EventHeader) == 16, "EventHeader debe medir 16 bytes");
static_assert(sizeof(Event) == 16 + kNumSamples * (1 + kNumChannels) * 4,
              "Event con layout inesperado");

static const size_t kBytesPerEvent = sizeof(Event);

#endif

#ifndef DRS_MUONES_CONFIG_H
#define DRS_MUONES_CONFIG_H

/* ============================================================================
 *
 *                  CONFIGURACION DE LA MEDICION  (drs_muones)
 *
 *  Este es el UNICO archivo que hay que tocar para cambiar como se mide.
 *
 *  COMO SE USA
 *    1. Cambiar el valor que esta a la derecha del nombre.
 *    2. Guardar el archivo.
 *    3. Correr   ./medir.sh   desde la carpeta nuevo_codigo.
 *       (medir.sh vuelve a compilar solo y despues empieza a medir)
 *
 *  REGLAS PARA NO ROMPER NADA
 *    - Cambiar solo el valor. No borrar "#define" ni el nombre.
 *    - Los decimales van con PUNTO:   0.6    (no 0,6)
 *    - Los textos van entre comillas dobles:   "medidas"
 *    - Lo que esta entre / *  y  * / son comentarios: el programa los ignora.
 *    - Si un valor no tiene sentido, al compilar aparece un mensaje en
 *      castellano que dice cual es y por que.
 *
 *  La explicacion larga de cada parametro esta en Docs/manual_drs_muones.pdf
 *
 * ========================================================================== */


/* ----------------------------------------------------------------------------
 *  1. DONDE SE GUARDAN LOS DATOS
 * ------------------------------------------------------------------------- */

/* Carpeta donde se guardan los archivos. Si no existe, se crea sola.
 * Los archivos quedan ADENTRO de esta carpeta, con nombres del tipo
 *     run_AAAAMMDD_HHMMSS_idx0.bin
 * Ejemplos:   "medidas"     "ang_0"     "eficiencia/123"
 * Si se usa ./medir.sh, la carpeta se crea adentro de nuevo_codigo.          */
#define CARPETA_SALIDA            "medidas"

/* Cada cuantos minutos se cierra el archivo y se empieza uno nuevo
 * (..._idx0.bin, ..._idx1.bin, ...). Si se corta la luz, a lo sumo se
 * pierde el archivo que estaba abierto.                                      */
#define MINUTOS_POR_ARCHIVO       15


/* ----------------------------------------------------------------------------
 *  2. MUESTREO
 * ------------------------------------------------------------------------- */

/* Frecuencia de muestreo en GHz (entre 0.1 y 6). Cada evento son siempre
 * 1024 muestras, asi que la frecuencia fija cuanto tiempo se ve:
 *
 *      frecuencia     paso entre muestras     duracion de la ventana
 *       0.6 GHz            1.66 ns                  1700 ns
 *       1.0 GHz            1.00 ns                  1024 ns
 *       2.0 GHz            0.50 ns                   512 ns
 *       5.0 GHz            0.20 ns                   205 ns                 */
#define FRECUENCIA_GHZ            0.6


/* ----------------------------------------------------------------------------
 *  3. TRIGGER (cuando se toma un evento)
 *     El trigger lo da siempre el canal 1 (CH1).
 * ------------------------------------------------------------------------- */

/* Umbral del trigger, en mV. Se toma un evento cuando CH1 cruza este valor. */
#define UMBRAL_TRIGGER_MV         -125

/* Hacia donde van los pulsos:
 *     1 = pulsos negativos (lo normal con un fotomultiplicador)
 *     0 = pulsos positivos
 * Tiene que ser coherente con los umbrales: pulsos negativos, umbrales
 * negativos. Vale para el trigger y para el filtro de la seccion 4.          */
#define PULSOS_NEGATIVOS          1

/* En que lugar de la ventana aparece el pulso que dio el trigger, en ns,
 * contado DESDE EL COMIENZO de la ventana (0 = borde izquierdo del grafico).
 *
 *   Ejemplo a 0.6 GHz (ventana de 1700 ns): con 300, el pulso de CH1 aparece
 *   en t = 300 ns; quedan 300 ns antes del pulso y 1400 ns despues.
 *
 * No se puede elegir cualquier valor: la placa solo llega a un rango, que
 * depende de la frecuencia (a 0.6 GHz, entre ~220 y ~1630 ns). Si se pide
 * algo fuera de ese rango, el programa usa el valor mas cercano y avisa.
 * Al arrancar siempre imprime en que posicion va a quedar el pulso.          */
#define POSICION_PULSO_NS         300


/* ----------------------------------------------------------------------------
 *  4. FILTRO (que eventos se guardan)
 *
 *     Sin filtro se guarda todo lo que dispara el trigger de CH1.
 *     Con filtro, cada evento se revisa antes de guardarlo y se descarta si
 *     no cumple lo que se pide aca abajo.
 * ------------------------------------------------------------------------- */

/* 0 = sin filtro: se guarda todo.
 * 1 = con filtro: se guardan solo los eventos que cumplen las condiciones.   */
#define FILTRO_ACTIVADO           0

/* Que se le pide a cada canal. Hay tres opciones:
 *
 *     REQUERIDO = tiene que haber un pulso en este canal; si no, se descarta
 *     IGNORAR   = da lo mismo lo que pase en este canal
 *     VETO      = si hay un pulso en este canal, el evento se descarta
 *
 * "Hay un pulso" quiere decir que el canal cruzo su umbral (los de abajo).   */
#define FILTRO_CH1                REQUERIDO
#define FILTRO_CH2                REQUERIDO
#define FILTRO_CH3                IGNORAR
#define FILTRO_CH4                VETO

/* Umbral de cada canal para decidir si tuvo un pulso, en mV.
 * Conviene que quede bien por afuera del ruido del canal.                    */
#define UMBRAL_CH1_MV             -125
#define UMBRAL_CH2_MV             -125
#define UMBRAL_CH3_MV             -125
#define UMBRAL_CH4_MV             -125

/* Ventana de coincidencia, en ns. Los pulsos de los canales REQUERIDO tienen
 * que empezar con una diferencia menor o igual que este valor. Si estan mas
 * separados, se considera que no son del mismo muon y el evento se descarta.
 * (El VETO no usa esta ventana: un pulso en cualquier lugar ya descarta.)    */
#define VENTANA_COINCIDENCIA_NS   50


/* ----------------------------------------------------------------------------
 *  5. LO QUE SE VE EN PANTALLA
 * ------------------------------------------------------------------------- */

/* Cada cuantos segundos se imprime la linea de estado.                       */
#define SEGUNDOS_ENTRE_ESTADOS    5

/* 1 = imprime una linea por cada trigger, diciendo si se guardo o por que
 *     se descarto. Util para probar el filtro. En corridas largas dejar 0.   */
#define IMPRIMIR_CADA_EVENTO      0


/* ----------------------------------------------------------------------------
 *  6. AVANZADO  (normalmente NO hace falta tocar nada de aca para abajo)
 * ------------------------------------------------------------------------- */

/* Si alguna muestra de algun canal supera este valor (en mV, sin signo), el
 * evento se marca como "saturado". No se descarta: es solo una marca.        */
#define SATURACION_MV             480

/* Cuantos eventos pueden esperar en memoria a que se escriban al disco.
 * Cada evento ocupa unos 20 KB (2000 eventos = 41 MB de RAM).                */
#define EVENTOS_EN_COLA           2000

/* 1 = al cerrar cada archivo se obliga al sistema a escribirlo fisicamente
 *     en el disco (mas seguro ante un corte de luz).                         */
#define FORZAR_ESCRITURA_A_DISCO  1

/* Cada cuantos microsegundos se le pregunta a la placa si hubo un trigger.   */
#define ESPERA_SONDEO_US          500

/* Calibracion de la posicion del pulso. La placa atrasa el trigger en pasos
 * ("ticks", de 0 a 255). Medido con esta placa (serie 2566) el 06/11/2026:
 * cada tick corre el pulso 5.54 ns. Si POSICION_PULSO_NS no coincide con lo
 * que se ve en los datos, se corrige aca (ver el manual, seccion del
 * trigger).                                                                  */
#define NS_POR_TICK               5.54

#endif

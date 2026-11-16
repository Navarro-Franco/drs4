# Scripts para mirar los datos

Todos se usan igual: se abre el script con un editor de texto, se cambian las
líneas marcadas con **LO UNICO QUE HAY QUE CAMBIAR** y se corre desde una
terminal parado en esta carpeta:

```
cd nuevo_codigo/scripts
python3 01_ver_un_evento.py
```

Necesitan `numpy` y `matplotlib` (`sudo apt install python3-numpy python3-matplotlib`).

| Script | Qué hace |
|---|---|
| `01_ver_un_evento.py` | **Empezar por acá.** Abre un `.bin`, muestra sus datos y grafica un evento. Explica paso a paso cómo se lee el archivo. |
| `02_filtrar_eventos.py` | Recorre una carpeta entera y se queda con los eventos que cumplen un criterio (pulso en CH1 y CH2 cercanos, nada en CH4, …). Es el mismo filtro del programa de medición, aplicado después. |
| `03_exportar_a_texto.py` | Pasa un evento a un archivo de texto con columnas, para Origin, Excel, etc. |
| `04_tasa_por_hora.py` | Calcula la tasa de eventos en cada hora del día y a lo largo de toda la medición, usando la hora guardada en cada evento. |
| `read_bin.py` | Más avanzado: corrige los tiempos de un archivo. Qué hace y sus límites: `correccion_temporal.html` (abrirlo con el navegador). |
| `curar_datos.py` | Grafica todos los eventos de un archivo, uno atrás del otro. |

## Qué es un archivo binario

Un archivo de texto guarda los números como letras: el número `-125.5` ocupa
seis caracteres y cualquiera lo puede leer con un editor. Un archivo **binario**
guarda los números tal como los tiene la computadora en memoria: cada número
ocupa siempre la misma cantidad de bytes (acá, 4). Ocupa menos lugar y se lee
mucho más rápido, pero si se lo abre con un editor de texto se ven símbolos
raros. Para leerlo hay que saber **en qué orden** están guardadas las cosas.
Eso es lo que sigue.

## Cómo es un `.bin` de este programa

```
+-----------------------------+
| encabezado del archivo      |  128 bytes, una sola vez
+-----------------------------+
| evento 0                    |  20 496 bytes
+-----------------------------+
| evento 1                    |  20 496 bytes
+-----------------------------+
| ...                         |
```

**Encabezado del archivo (128 bytes).** Datos generales de la medición:

| Bytes | Qué hay | Tipo |
|---|---|---|
| 0 a 7 | el texto `MUONBIN1` | texto |
| 8 a 9 | versión del formato (1 o 2) | entero de 2 bytes |
| 10 a 11 | cantidad de canales (4) | entero de 2 bytes |
| 12 a 15 | muestras por evento (1024) | entero de 4 bytes |
| 16 a 23 | frecuencia de muestreo, en Hz | decimal de 8 bytes |
| 24 a 25 | número de serie de la placa | entero de 2 bytes |
| 26 a 27 | versión de firmware de la placa | entero de 2 bytes |
| 28 a 31 | marcas del archivo (ver abajo) | entero de 4 bytes |
| 32 a 39 | cuándo se abrió el archivo (ns desde 1970) | entero de 8 bytes |
| 40 a 103 | configuración resumida, por ejemplo `thr=-125mV pos=300ns ...` | texto |
| 104 a 127 | sin uso | — |

**Cada evento (20 496 bytes).** Un encabezado corto y la tabla de mediciones:

| Bytes | Qué hay | Tipo |
|---|---|---|
| 0 a 7 | hora del trigger (ns desde 1970) | entero de 8 bytes |
| 8 a 11 | número de trigger dentro de la corrida | entero de 4 bytes |
| 12 a 13 | celda del chip donde frenó el muestreo | entero de 2 bytes |
| 14 a 15 | marcas del evento (ver abajo) | entero de 2 bytes |
| 16 en adelante | tabla de 1024 filas × 5 columnas: `t [ns]`, `CH1`, `CH2`, `CH3`, `CH4` `[mV]` | decimales de 4 bytes |

La cuenta cierra: 16 + 1024 × 5 × 4 = 20 496 bytes. Y la cantidad de eventos
de un archivo es (tamaño del archivo − 128) / 20 496.

En Python todo eso se le dice a `numpy` con un "molde" (está en los scripts):

```python
molde = np.dtype([('hora', '<u8'), ('numero', '<u4'), ('celda', '<u2'),
                  ('marcas', '<u2'), ('datos', '<f4', (1024, 5))])
eventos = np.fromfile(archivo, dtype=molde, offset=128)

t   = eventos[0]['datos'][:, 0]     # tiempos del primer evento
ch1 = eventos[0]['datos'][:, 1]     # CH1 del primer evento
```

## Las marcas

Las "marcas" son un número donde cada bit es un sí/no.

Marcas del **evento**:

| Para saber si… | Cuenta en Python |
|---|---|
| algún canal saturó | `marcas & 1` |
| CH1 tuvo pulso | `marcas & 2` |
| CH2 tuvo pulso | `marcas & 4` |
| CH3 tuvo pulso | `marcas & 8` |
| CH4 tuvo pulso | `marcas & 16` |

Si la cuenta da 0 es "no"; si da otra cosa es "sí". Ejemplo: cuántos eventos
de un archivo tienen pulso en CH2: `np.count_nonzero(eventos['marcas'] & 4)`.

Marcas del **archivo** (bytes 28 a 31 del encabezado): `& 1` = los tiempos
usan la calibración de la placa; `& 2` = se midió con el filtro activado.

Las marcas de pulso y las del archivo existen desde la versión 2 del formato
(14/11/2026). En los archivos anteriores valen siempre 0, aunque haya pulsos.

## La hora de cada evento

Cada evento guarda cuándo fue el trigger, en el campo `hora`. Es un número
muy grande: los nanosegundos que pasaron desde el 1/1/1970 a las 0:00 UTC (así
guardan las fechas las computadoras). Para verlo como fecha y hora:

```python
import datetime
print(datetime.datetime.fromtimestamp(eventos['hora'][0] / 1e9))
```

Eso da la fecha en la hora local de la computadora donde se corre el script.
Los archivos viejos también la tienen.

La hora sale del reloj de la computadora que midió, así que es tan buena como
ese reloj. Una Raspberry Pi sin internet pierde la hora al apagarse: antes de
medir conviene escribir `date` en la terminal y ver que esté bien.

## El número de trigger y los eventos descartados

`numero` cuenta **todos** los triggers de la corrida, también los que el
filtro descartó. Por eso, en una medición con filtro los números guardados
saltan (0, 1, 4, 7, …): los que faltan son los descartados. El total de
triggers y de descartados de la corrida está en el archivo `..._info.txt`.

## Archivos viejos

Los `.bin` medidos antes del 14/11/2026 (versión 1) se leen con los mismos
scripts. Dos diferencias:

- En el comentario dicen `delay=...ns`. Ese número **no** es la posición del
  pulso: es, aproximadamente, el tiempo entre el pulso y el **final** de la
  ventana. Con `delay=600ns` a 0,6 GHz el pulso está en t ≈ 1100 ns.
- Algunos quedaron con el nombre de la carpeta pegado adelante
  (`ang_1run_...bin`), por un error del programa que ya está corregido.

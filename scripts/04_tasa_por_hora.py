# =============================================================================
#  04 - TASA DE EVENTOS SEGUN LA HORA DEL DIA
#
#  Recorre todos los .bin de una carpeta y calcula cuantos eventos por segundo
#  hubo en cada hora del dia (de 0 a 1 h, de 1 a 2 h, ... de 23 a 24 h), para
#  ver si la tasa de muones cambia a lo largo del dia.
#
#  De donde sale la hora: cada evento tiene guardado el momento del trigger
#  (el campo 'hora'), como la cantidad de nanosegundos que pasaron desde el
#  1/1/1970 a las 0:00 UTC. Es la forma habitual en que una computadora guarda
#  una fecha. Mas abajo se pasa a "hora del dia".
#
#  OJO con tres cosas:
#   - La hora es la del reloj de la computadora que midio. Si ese reloj estaba
#     mal, las horas estan mal. Una Raspberry Pi sin internet pierde la hora
#     al apagarse: antes de medir conviene escribir  date  en la terminal.
#   - Si se midio con el filtro activado, esta es la tasa de eventos
#     GUARDADOS, no la de todos los triggers.
#   - No mezclar en la misma carpeta mediciones hechas con distinta
#     configuracion (umbral, tension, posicion de los detectores): la tasa
#     cambia por eso y no por la hora.
#
#  Como se usa:
#     1. Cambiar las lineas de configuracion de aca abajo.
#     2. Parado en la carpeta scripts:   python3 04_tasa_por_hora.py
# =============================================================================

import datetime
import glob
import os

import numpy as np
import matplotlib.pyplot as plt

# ---------------- LO UNICO QUE HAY QUE CAMBIAR ----------------
carpeta = "../datos_obtenidos/eficiencia/132"    # carpeta con los .bin

# Diferencia horaria con UTC, en horas. Argentina = -3
diferencia_con_utc = -3

# Si entre un evento y el siguiente pasan mas segundos que esto, se supone que
# en el medio NO se estaba midiendo (se corto la medicion y se volvio a
# empezar). Ese tiempo no se cuenta. Tiene que ser bastante mas grande que el
# tiempo normal entre dos eventos.
corte_s = 120
# ---------------------------------------------------------------

# El "molde" de un evento (explicado en 01_ver_un_evento.py)
molde = np.dtype([('hora', '<u8'), ('numero', '<u4'), ('celda', '<u2'),
                  ('marcas', '<u2'), ('datos', '<f4', (1024, 5))])


# --- PASO 1: juntar la hora de todos los eventos de la carpeta ----------------
archivos = sorted(glob.glob(carpeta + "/*.bin"))
print("archivos encontrados:", len(archivos))

lista = []
for archivo in archivos:
    # cuantos eventos enteros tiene el archivo (128 = encabezado)
    cantidad = (os.path.getsize(archivo) - 128) // molde.itemsize
    if cantidad <= 0:
        continue
    # np.memmap es como np.fromfile, pero no carga el archivo entero: solo lee
    # lo que se le pide. Aca se pide nada mas que la hora de cada evento.
    eventos = np.memmap(archivo, dtype=molde, mode='r', offset=128, shape=(cantidad,))
    lista.append(np.array(eventos['hora']))

# todas las horas juntas, en segundos y ordenadas de la mas vieja a la mas nueva
horas = np.sort(np.concatenate(lista)) / 1e9

# se corre el reloj para pasar de UTC a la hora local
horas = horas + diferencia_con_utc * 3600

origen = datetime.datetime(1970, 1, 1)
print("eventos             :", len(horas))
print("primer evento       :", origen + datetime.timedelta(seconds=horas[0]))
print("ultimo evento       :", origen + datetime.timedelta(seconds=horas[-1]))


# --- PASO 2: tiempo entre eventos seguidos ------------------------------------
# dt[i] = segundos entre el evento i y el evento i+1
dt = np.diff(horas)

# Un intervalo "vale" si es corto: quiere decir que se estuvo midiendo todo ese
# rato. Los intervalos largos son cortes de la medicion y se descartan.
vale = dt < corte_s

# Cada intervalo que vale aporta: 1 evento (el que lo termina) y dt segundos
# de tiempo medido. Se anotan a nombre del momento en que termina.
momento = horas[1:][vale]
duracion = dt[vale]

print("cortes detectados   :", np.count_nonzero(~vale))
print("tiempo medido       :", round(duracion.sum() / 3600, 2), "horas")

tasa_media = len(duracion) / duracion.sum()
print("tasa media          :", round(tasa_media, 4), "eventos por segundo")


# --- PASO 3: tasa en cada hora del dia ----------------------------------------
# Un dia tiene 86400 segundos. El resto de dividir por 86400 son los segundos
# desde la medianoche; dividido 3600 da la hora del dia (de 0 a 24).
hora_del_dia = (momento % 86400) / 3600
franja = hora_del_dia.astype(int)          # 0, 1, 2, ..., 23

x = []
tasa = []
error = []
print()
print("franja     eventos   minutos medidos   eventos por segundo")
for h in range(24):
    de_esta_franja = (franja == h)
    n = np.count_nonzero(de_esta_franja)       # eventos en esta franja
    segundos = duracion[de_esta_franja].sum()  # tiempo medido en esta franja
    if segundos == 0:
        continue                               # a esta hora nunca se midio
    x.append(h + 0.5)
    tasa.append(n / segundos)
    # error estadistico de contar n eventos: raiz de n
    error.append(np.sqrt(n) / segundos)
    print("%2d a %2d h  %7d   %15.1f   %.4f +- %.4f"
          % (h, h + 1, n, segundos / 60, n / segundos, np.sqrt(n) / segundos))


# --- PASO 4: tasa a lo largo de toda la medicion (un punto por hora) ----------
# Sirve para ver si un cambio se repite todos los dias a la misma hora o si
# fue cosa de un solo dia.
bloque = (momento // 3600).astype(int)     # numero de hora desde 1970
fechas = []
tasa_bloque = []
error_bloque = []
for b in np.unique(bloque):
    de_este_bloque = (bloque == b)
    n = np.count_nonzero(de_este_bloque)
    segundos = duracion[de_este_bloque].sum()
    if segundos < 600:
        continue        # menos de 10 minutos medidos en esa hora: no se grafica
    fechas.append(origen + datetime.timedelta(hours=int(b), minutes=30))
    tasa_bloque.append(n / segundos)
    error_bloque.append(np.sqrt(n) / segundos)


# --- PASO 5: graficos ---------------------------------------------------------
plt.figure()
plt.errorbar(x, tasa, yerr=error, fmt='o', capsize=3)
plt.axhline(tasa_media, color='gray', linestyle='--', label='tasa media')
plt.xlim(0, 24)
plt.xticks(range(0, 25, 2))
plt.xlabel('hora del dia')
plt.ylabel('eventos por segundo')
plt.title('tasa segun la hora del dia')
plt.legend()
plt.grid(True)

plt.figure()
plt.errorbar(fechas, tasa_bloque, yerr=error_bloque, fmt='.', capsize=2)
plt.axhline(tasa_media, color='gray', linestyle='--', label='tasa media')
plt.xlabel('fecha y hora')
plt.ylabel('eventos por segundo')
plt.title('tasa a lo largo de la medicion (un punto por hora)')
plt.gcf().autofmt_xdate()
plt.legend()
plt.grid(True)

plt.show()

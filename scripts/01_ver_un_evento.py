# =============================================================================
#  01 - VER UN EVENTO
#
#  Abre un archivo .bin, muestra los datos generales y grafica un evento.
#
#  Como se usa:
#     1. Cambiar "archivo" y "evento" aca abajo.
#     2. En una terminal, parado en la carpeta scripts:   python3 01_ver_un_evento.py
#
#  La explicacion de que hay adentro de un .bin esta en LEEME.md
# =============================================================================

import struct

import numpy as np
import matplotlib.pyplot as plt

# ---------------- LO UNICO QUE HAY QUE CAMBIAR ----------------
archivo = "../datos_obtenidos/eficiencia/132/run_20261030_091101_idx0.bin"
evento = 0          # cual evento graficar: 0 es el primero del archivo
# ---------------------------------------------------------------


# --- PASO 1: leer el encabezado -----------------------------------------------
# Los primeros 128 bytes del archivo son el "encabezado": no son mediciones,
# son datos sobre la medicion (frecuencia, numero de placa, configuracion).

f = open(archivo, 'rb')          # 'rb' = abrir para leer ("r") un binario ("b")
encabezado = f.read(128)
f.close()

# struct.unpack convierte bytes en numeros. El primer texto dice que tipo de
# numero es:  '<H' = entero de 2 bytes,  '<I' = entero de 4 bytes,
#             '<Q' = entero de 8 bytes,  '<d' = numero con coma de 8 bytes.
# Los corchetes [10:12] dicen de que byte a que byte esta guardado.
formato       = encabezado[0:8].decode()                    # tiene que decir MUONBIN1
version       = struct.unpack('<H', encabezado[8:10])[0]
n_canales     = struct.unpack('<H', encabezado[10:12])[0]   # 4
n_muestras    = struct.unpack('<I', encabezado[12:16])[0]   # 1024
frecuencia_hz = struct.unpack('<d', encabezado[16:24])[0]
placa         = struct.unpack('<H', encabezado[24:26])[0]
comentario    = encabezado[40:104].split(b'\0')[0].decode()

print("archivo      :", archivo)
print("formato      :", formato, "version", version)
print("frecuencia   :", frecuencia_hz / 1e9, "GHz")
print("canales      :", n_canales, "  muestras por evento:", n_muestras)
print("placa        :", placa)
print("configuracion:", comentario)


# --- PASO 2: leer los eventos -------------------------------------------------
# Despues del encabezado vienen los eventos, uno atras del otro, todos del
# mismo tamaño. Aca se le describe a numpy como es UN evento (el "molde"):
molde = np.dtype([
    ('hora',    '<u8'),   # cuando fue el trigger (nanosegundos desde 1970)
    ('numero',  '<u4'),   # numero de trigger dentro de la corrida
    ('celda',   '<u2'),   # dato interno del chip (no hace falta para graficar)
    ('marcas',  '<u2'),   # marcas del evento (ver LEEME.md)
    ('datos',   '<f4', (n_muestras, 1 + n_canales)),   # la tabla de mediciones
])

# offset=128 quiere decir "saltear los 128 bytes del encabezado"
eventos = np.fromfile(archivo, dtype=molde, offset=128)

print("eventos      :", len(eventos))


# --- PASO 3: elegir un evento y separar las columnas --------------------------
# 'datos' es una tabla de 1024 filas y 5 columnas:
#     columna 0 = tiempo en ns
#     columna 1 = CH1 en mV,  columna 2 = CH2,  columna 3 = CH3,  columna 4 = CH4

e = eventos[evento]
tabla = e['datos']

t   = tabla[:, 0]
ch1 = tabla[:, 1]
ch2 = tabla[:, 2]
ch3 = tabla[:, 3]
ch4 = tabla[:, 4]

print()
print("evento", evento, "-> trigger numero", e['numero'])
print("minimo de CH1:", ch1.min(), "mV, en t =", t[ch1.argmin()], "ns")


# --- PASO 4: graficar ---------------------------------------------------------
plt.plot(t, ch1, label='CH1')
plt.plot(t, ch2, label='CH2')
plt.plot(t, ch3, label='CH3')
plt.plot(t, ch4, label='CH4')
plt.xlabel('tiempo [ns]')
plt.ylabel('señal [mV]')
plt.title('evento ' + str(evento))
plt.legend()
plt.grid(True)
plt.show()

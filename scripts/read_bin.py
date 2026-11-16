import re
import struct

import numpy as np
import matplotlib.pyplot as plt

# Correccion temporal por archivo + grafico de cada evento.
# Que hace y cuanto se le puede creer: ver correccion_temporal.html
# (Para solo mirar un evento, es mas simple 01_ver_un_evento.py)
#
# Editar estas lineas antes de correr el script (se corre parado en scripts/)
#archivo = "/home/franconavarrro/Documents/labo_5/muones/data_nuestra/test_trigger/trigger_100/trigger_delay_100run_20261106_081032_idx0.bin"
#archivo = "/home/franconavarrro/Documents/labo_5/muones/data_nuestra/test_trigger/trigger_500/trigger_delay_500run_20261106_082801_idx0.bin"
#archivo = "/home/franconavarrro/Documents/labo_5/muones/data_nuestra/test_trigger/trigger_1000/trigger_delay_1000run_20261106_084551_idx0.bin"
archivo = "../datos_obtenidos/eficiencia/132/run_20261030_091101_idx0.bin"
n_puntos = 12    # puntos de la curva de correccion

f = open(archivo, 'rb')
magic, version, nch, nsamp = struct.unpack('<8sHHI', f.read(16))
fs_hz, = struct.unpack('<d', f.read(8))
serial, fw, flags, inicio = struct.unpack('<HHIQ', f.read(16))
comentario = f.read(64).split(b'\0')[0].decode()
f.close()

evento_t = np.dtype([('ts', '<u8'), ('idx', '<u4'), ('tc', '<u2'), ('fl', '<u2'),
                     ('s', '<f4', (nsamp, 1 + nch))])

datos = np.memmap(archivo, dtype=evento_t, mode='r', offset=128)

print("archivo     :", archivo)
print("formato     :", magic.decode(), "v", version)
print("frecuencia  :", fs_hz / 1e9, "GHz")
print("canales     :", nch, " muestras:", nsamp)
print("board/fw    :", serial, fw)
print("comentario  :", comentario)
print("eventos     :", len(datos))

# ---------------------------------------------------------------------------
# 1) Curva de correccion I(c): se calcula UNA vez, con todos los eventos del
#    archivo.
# ---------------------------------------------------------------------------

# Donde cruza CH1 el umbral de trigger en cada evento de este archivo.
# Se ignoran 8 celdas en cada borde de la ventana.
umbral = float(re.search(r'thr=(-?[\d.]+)', comentario).group(1))
delta = 1e9 / fs_hz
tc = []
x = []
for d in datos:
    v = d['s'][8:nsamp - 8, 1]
    cruces = np.flatnonzero((v[:-1] > umbral) & (v[1:] <= umbral))
    if len(cruces) > 0:
        j = cruces[0]
        tc.append(float(d['tc']))
        x.append(8 + j + float((v[j] - umbral) / (v[j] - v[j + 1])))
tc = np.array(tc)
x = np.array(x)


# Cuanto pesa cada punto de la curva de correccion I en la celda c.
def pesos(c):
    nodos = np.arange(n_puntos) * nsamp / n_puntos
    return np.column_stack([np.interp(c, nodos, np.eye(n_puntos)[k], period=nsamp)
                            for k in range(n_puntos)])


hay_correccion = len(x) >= 40
if not hay_correccion:
    print("correccion  : no se aplica, solo hay", len(x), "eventos con cruce de CH1")
else:
    # El tiempo real entre el cruce de CH1 y el fin de la ventana es fijo:
    #   (nsamp - x) * delta + I(tc) - I(tc + x) = T0
    A = np.column_stack([pesos(tc) - pesos(tc + x), -np.ones(len(x))])
    y = -(nsamp - x) * delta
    usar = np.ones(len(x), dtype=bool)
    for _ in range(4):
        c = np.linalg.lstsq(A[usar], y[usar], rcond=1e-3)[0]
        r = A @ c - y
        ancho = np.subtract(*np.percentile(r[usar], [84, 16])) / 2
        usar = np.abs(r) < 4 * max(ancho, 0.2)
    I = c[:n_puntos]

    antes = np.subtract(*np.percentile(x * delta, [84, 16])) / 2
    despues = np.subtract(*np.percentile(r, [84, 16])) / 2
    print("correccion  :", len(x), "eventos con cruce de CH1, T0 =", round(c[n_puntos], 1), "ns")
    print("dispersion  :", round(antes, 2), "->", round(despues, 2), "ns")

# ---------------------------------------------------------------------------
# 2) Graficar los eventos de a uno, con el tiempo corregido.
#    (Cerrar la ventana del grafico para pasar al siguiente; Ctrl+C en la
#    terminal para cortar.)
# ---------------------------------------------------------------------------

for evento in range(len(datos)):
    e = datos[evento]
    t = np.array(e['s'][:, 0], dtype=float)

    print("evento      :", e['idx'], " trigger_cell:", e['tc'], " flags:", e['fl'])

    if hay_correccion:
        # Tiempo corregido de este evento.
        celdas = float(e['tc']) + np.arange(nsamp)
        t_archivo = t
        t = np.arange(nsamp) * delta + pesos(celdas) @ I - pesos([float(e['tc'])])[0] @ I
        print("corrimiento :", round(np.abs(t - t_archivo).max(), 2), "ns como maximo en este evento")
        if np.ptp(np.diff(t_archivo)) > 1e-3:
            print("aviso       : el t del archivo ya usaba la calibracion de la placa;")
            print("              se reemplaza por esta estimacion, que puede ser peor")

    plt.plot(t, e['s'][:, 1], label='ch1')
    plt.plot(t, e['s'][:, 2], label='ch2')
    plt.plot(t, e['s'][:, 3], label='ch3')
    plt.plot(t, e['s'][:, 4], label='ch4')
    plt.xlabel('t [ns]')
    plt.ylabel('mV')
    plt.legend()
    plt.grid(True)
    plt.show()

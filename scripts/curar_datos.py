import struct

import numpy as np
import matplotlib.pyplot as plt

# Editar estas dos lineas antes de correr el script
ruta = "./datos_obtenidos/eficiencia/"
file = "123/run_20261030_083445_idx0.bin"
archivo = ruta + file
evento = 0

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

print("================================================================================")
for evento in range(len(datos)):
    e = datos[evento]
    t = e['s'][:, 0]
    print("evento      :", e['idx'], " trigger_cell:", e['tc'], " flags:", e['fl'])
    plt.plot(t, e['s'][:, 1], marker='o', linestyle=' ', label='ch1')
    plt.plot(t, e['s'][:, 2], marker='o', linestyle=' ', label='ch2')
    plt.plot(t, e['s'][:, 3], marker='o', linestyle=' ', label='ch3')
    plt.plot(t, e['s'][:, 4], marker='o', linestyle=' ', label='ch4')
    plt.xlabel('t [ns]')
    plt.ylabel('mV')
    plt.legend()
    plt.grid(True)
    plt.show()

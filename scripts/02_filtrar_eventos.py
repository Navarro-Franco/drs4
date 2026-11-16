# =============================================================================
#  02 - FILTRAR EVENTOS YA MEDIDOS
#
#  Recorre todos los .bin de una carpeta y se queda con los eventos que cumplen
#  un criterio. Es el MISMO criterio que usa el programa de medicion cuando en
#  config.h se pone FILTRO_ACTIVADO 1, pero aplicado despues, sobre datos que
#  se guardaron sin filtrar. Sirve para probar distintos criterios sin volver
#  a medir.
#
#  Que hace con cada evento:
#     - Busca en cada canal si la señal cruza su umbral ("hay un pulso") y en
#       que tiempo lo cruza.
#     - VETO:      si hay pulso en ese canal, descarta el evento.
#     - REQUERIDO: si NO hay pulso en ese canal, descarta el evento.
#     - Los pulsos de los canales REQUERIDO tienen que empezar a menos de
#       "ventana_ns" uno del otro; si no, descarta el evento.
#
#  Como se usa:
#     1. Cambiar las lineas de configuracion de aca abajo.
#     2. Parado en la carpeta scripts:   python3 02_filtrar_eventos.py
# =============================================================================

import glob
import os

import numpy as np
import matplotlib.pyplot as plt

# ---------------- LO UNICO QUE HAY QUE CAMBIAR ----------------
carpeta = "../datos_obtenidos/eficiencia/132"    # carpeta con los .bin

#            CH1          CH2          CH3        CH4
pedido  = ["REQUERIDO", "REQUERIDO", "IGNORAR", "VETO"]
umbral  = [-125,        -125,        -125,      -125]     # mV, uno por canal

ventana_ns = 50          # separacion maxima entre los pulsos REQUERIDO

# True = ademas guarda los eventos que pasan en una carpeta nueva, en el mismo
#        formato .bin, para poder abrirlos con los otros scripts.
guardar_filtrados = False
carpeta_filtrados = "../datos_filtrados"
# ---------------------------------------------------------------

borde = 8    # muestras que no se miran en cada borde (ahi suele haber picos falsos)

# El "molde" de un evento (explicado en 01_ver_un_evento.py)
molde = np.dtype([('hora', '<u8'), ('numero', '<u4'), ('celda', '<u2'),
                  ('marcas', '<u2'), ('datos', '<f4', (1024, 5))])

archivos = sorted(glob.glob(carpeta + "/*.bin"))
print("archivos encontrados:", len(archivos))

total = 0
guardados = 0
por_veto = 0
por_falta = 0
por_ventana = 0
diferencias = []     # t(CH2) - t(CH1) de los eventos donde los dos tienen pulso

for archivo in archivos:
    eventos = np.fromfile(archivo, dtype=molde, offset=128)
    pasa = np.zeros(len(eventos), dtype=bool)    # True = el evento se guarda

    for n in range(len(eventos)):
        tabla = eventos[n]['datos']
        t = tabla[:, 0]

        # Para cada canal: ¿hay pulso?, ¿en que tiempo empieza?
        hay_pulso = [False, False, False, False]
        t_pulso = [0.0, 0.0, 0.0, 0.0]
        for c in range(4):
            v = tabla[:, 1 + c]
            # posiciones (sin contar los bordes) donde la señal paso el umbral
            # (para pulsos positivos, cambiar <= por >=)
            pasaron = np.flatnonzero(v[borde:-borde] <= umbral[c])
            if len(pasaron) > 0:
                i = pasaron[0] + borde        # la primera muestra que lo paso
                hay_pulso[c] = True
                t_pulso[c] = t[i]
                # un poco mas fino: el cruce esta entre la muestra i-1 y la i
                if i > borde and v[i] != v[i - 1]:
                    fraccion = (umbral[c] - v[i - 1]) / (v[i] - v[i - 1])
                    t_pulso[c] = t[i - 1] + fraccion * (t[i] - t[i - 1])

        if hay_pulso[0] and hay_pulso[1]:
            diferencias.append(t_pulso[1] - t_pulso[0])

        # Decidir
        total = total + 1

        hay_veto = False
        falta_alguno = False
        tiempos_requeridos = []
        for c in range(4):
            if pedido[c] == "VETO" and hay_pulso[c]:
                hay_veto = True
            if pedido[c] == "REQUERIDO":
                if hay_pulso[c]:
                    tiempos_requeridos.append(t_pulso[c])
                else:
                    falta_alguno = True

        if hay_veto:
            por_veto = por_veto + 1
        elif falta_alguno:
            por_falta = por_falta + 1
        elif len(tiempos_requeridos) > 0 and max(tiempos_requeridos) - min(tiempos_requeridos) > ventana_ns:
            por_ventana = por_ventana + 1
        else:
            pasa[n] = True
            guardados = guardados + 1

    # Guardar los que pasaron en un archivo nuevo con el mismo formato:
    # primero el mismo encabezado de 128 bytes, despues los eventos elegidos.
    if guardar_filtrados and pasa.sum() > 0:
        os.makedirs(carpeta_filtrados, exist_ok=True)
        f = open(archivo, 'rb')
        encabezado = f.read(128)
        f.close()
        salida = open(carpeta_filtrados + "/" + os.path.basename(archivo), 'wb')
        salida.write(encabezado)
        eventos[pasa].tofile(salida)
        salida.close()

print()
print("eventos revisados           :", total)
print("pasan el filtro             :", guardados)
print("descartados por VETO        :", por_veto)
print("descartados, falta un pulso :", por_falta)
print("descartados, fuera ventana  :", por_ventana)
if guardar_filtrados:
    print("los que pasan quedaron en   :", carpeta_filtrados)

# Histograma de la diferencia de tiempo entre CH2 y CH1. Ayuda a elegir
# ventana_ns: los eventos "buenos" forman un pico angosto cerca de cero.
if len(diferencias) > 0:
    plt.hist(diferencias, bins=100, range=(-100, 100))
    plt.axvline(-ventana_ns, color='red', linestyle='--')
    plt.axvline(ventana_ns, color='red', linestyle='--', label='ventana de coincidencia')
    plt.xlabel('t(CH2) - t(CH1) [ns]')
    plt.ylabel('cantidad de eventos')
    plt.legend()
    plt.grid(True)
    plt.show()

# =============================================================================
#  03 - PASAR UN EVENTO A TEXTO
#
#  Guarda un evento en un archivo de texto con 5 columnas (t, CH1, CH2, CH3,
#  CH4), para abrirlo con Origin, Excel, una planilla o lo que se prefiera.
#
#  Como se usa:
#     1. Cambiar "archivo", "evento" y "salida" aca abajo.
#     2. Parado en la carpeta scripts:   python3 03_exportar_a_texto.py
# =============================================================================

import numpy as np

# ---------------- LO UNICO QUE HAY QUE CAMBIAR ----------------
archivo = "../datos_obtenidos/eficiencia/132/run_20261030_091101_idx0.bin"
evento = 0                    # cual evento exportar: 0 es el primero
salida = "evento_0.txt"       # nombre del archivo de texto que se crea
# ---------------------------------------------------------------

# El "molde" de un evento (explicado en 01_ver_un_evento.py)
molde = np.dtype([('hora', '<u8'), ('numero', '<u4'), ('celda', '<u2'),
                  ('marcas', '<u2'), ('datos', '<f4', (1024, 5))])

# offset=128: se saltea el encabezado del archivo
eventos = np.fromfile(archivo, dtype=molde, offset=128)
print("el archivo tiene", len(eventos), "eventos")

tabla = eventos[evento]['datos']      # 1024 filas x 5 columnas

np.savetxt(salida, tabla, fmt='%.3f', delimiter='\t',
           header='t[ns]\tCH1[mV]\tCH2[mV]\tCH3[mV]\tCH4[mV]', comments='')

print("evento", evento, "guardado en", salida)

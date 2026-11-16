# drs4 — medir muones con la DRS4 Evaluation Board

Programa de adquisición continua (`drs_muones`) para el Laboratorio 5 (experiencia de muones). Toma eventos de la placa DRS4 Evaluation Board (4 canales, trigger en CH1), los filtra opcionalmente y los guarda en archivos binarios `.bin`. Reemplaza al programa `drs_exam` del laboratorio.

## Qué hay en el repositorio

| Carpeta / archivo | Contenido |
|---|---|
| `src/` | Código en C++: adquisición (`main.cpp`), configuración (`config.h`), filtro (`filtro.h`) y formato de archivo (`binformat.h`). |
| `scripts/` | Ejemplos en Python para leer, filtrar, exportar y analizar los datos. |
| `Docs/` | Manual completo (PDF y fuente LaTeX). |
| `medir.sh` | Compila y mide. |
| `LEEME.md` | Guía de uso paso a paso, en castellano. |

## Requisitos

- Linux con `g++`, `cmake` y `libusb-1.0-0-dev`.
- El código oficial de la placa, repositorio [`drs4eb`](https://www.psi.ch/en/drs/software-download) (PSI), descargado **al lado** de esta carpeta, de modo que exista `../drs4eb/software`. Si está en otro lugar, indicar la ruta con `DRS4EB_DIR` en `CMakeLists.txt`.
- Para los scripts: `python3`, `numpy`, `matplotlib`.

## Uso rápido

1. Editar `src/config.h` (carpeta de salida, umbral, filtro, etc.).
2. Correr `./medir.sh`.
3. Terminar con `Ctrl+C` una sola vez.

Para probar sin la placa: `./medir.sh --selftest 100`.

Detalles en `LEEME.md` y `Docs/manual_drs_muones.pdf`.

## Características

- Medición continua, con cola en RAM y escritura en un hilo aparte (no se pierden eventos).
- Archivos binarios que se renuevan cada cierto tiempo, más un `_info.txt` con la configuración y el resumen.
- Posición del pulso en la ventana configurable.
- Filtro opcional por canal: requerido, ignorar o veto, con ventana de coincidencia.
- Modo `--selftest` sin placa.

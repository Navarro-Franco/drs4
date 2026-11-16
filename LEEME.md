# drs_muones — medir muones con la DRS4

Programa que toma eventos de la placa DRS4 Evaluation Board (4 canales, trigger
en CH1) y los guarda en archivos `.bin`.

## Uso en 3 pasos

1. **Configurar.** Abrir `src/config.h` con cualquier editor de texto y cambiar
   los valores que hagan falta (carpeta donde se guarda, umbral, filtro, etc.).
   Cada valor tiene su explicación al lado. Guardar.

2. **Medir.** En una terminal, parado en esta carpeta (`nuevo_codigo`):

   ```
   ./medir.sh
   ```

   `medir.sh` compila solo si cambió algo y después empieza a medir. Al
   arrancar muestra la configuración que está usando.

3. **Terminar.** Apretar `Ctrl+C` **una sola vez** y esperar el resumen final.

Para probar sin la placa conectada (genera pulsos inventados):

```
./medir.sh --selftest 100
```

## Qué queda guardado

En la carpeta elegida en `CARPETA_SALIDA` (por defecto `medidas/`):

| Archivo | Qué es |
|---|---|
| `run_FECHA_HORA_idx0.bin`, `_idx1.bin`, … | Los eventos. Se empieza un archivo nuevo cada `MINUTOS_POR_ARCHIVO`. |
| `run_FECHA_HORA_info.txt` | Texto con la configuración usada y, al terminar, el resumen: duración, triggers, eventos guardados y descartados. |

## Lo que más se cambia en `config.h`

| Parámetro | Para qué |
|---|---|
| `CARPETA_SALIDA` | Dónde se guardan los archivos (`"ang_0"`, `"eficiencia/123"`, …). |
| `UMBRAL_TRIGGER_MV` | A partir de qué altura de pulso en CH1 se toma un evento. |
| `POSICION_PULSO_NS` | En qué lugar de la ventana aparece el pulso, contado desde el comienzo. |
| `FILTRO_ACTIVADO` | `0` guarda todo; `1` guarda solo lo que cumple el filtro. |
| `FILTRO_CH1` … `FILTRO_CH4` | Qué se le pide a cada canal: `REQUERIDO`, `IGNORAR` o `VETO`. |
| `VENTANA_COINCIDENCIA_NS` | Cuánto pueden estar separados los pulsos de los canales `REQUERIDO`. |

## Mirar los datos

En `scripts/` hay ejemplos en Python, de menor a mayor dificultad. Empezar por
`scripts/LEEME.md`.

## Si algo no anda

| Qué pasa | Qué hacer |
|---|---|
| `ERROR: no se pudo compilar` | Casi siempre es un valor mal escrito en `config.h`. El mensaje dice la línea. |
| `No se encontro ninguna placa DRS4` | Revisar el cable USB. Si con `sudo ./medir.sh` anda, faltan los permisos de USB (manual, sección 3). |
| `triggers` no aumenta | No hay pulsos que pasen el umbral: revisar `UMBRAL_TRIGGER_MV` y los cables. |
| `guardados` no aumenta pero `triggers` sí | El filtro está descartando todo. Poner `IMPRIMIR_CADA_EVENTO 1` para ver por qué. |
| `permission denied: ./medir.sh` | Correr una vez `chmod +x medir.sh`. |

El manual completo está en `Docs/manual_drs_muones.pdf`.

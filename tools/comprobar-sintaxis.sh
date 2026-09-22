#!/usr/bin/env bash
# ============================================================================
#  Comprobación RÁPIDA de sintaxis del firmware, sin toolchain de ESP32
# ----------------------------------------------------------------------------
#  `pio run` necesita descargar el compilador de Xtensa y las librerías
#  (NimBLE, RadioLib). Cuando no hay red —o cuando sólo se quiere saber si un
#  cambio compila— este script pasa el g++ del sistema por los dos firmwares
#  usando las cabeceras de mentira de tools/stubs/.
#
#  QUÉ CAZA : erratas, variables sin declarar, funciones usadas antes de
#             existir, #if mal cerrados, argumentos de más o de menos.
#  QUÉ NO   : que el binario quepa, que la API real de NimBLE/RadioLib sea
#             exactamente esa, y por supuesto nada de lo que pase en la placa.
#
#  ⚠️ Los stubs NO se compilan nunca dentro del firmware: viven en tools/,
#     fuera de src/ y de lib/. Son sólo para este script.
#
#  Uso:  ./tools/comprobar-sintaxis.sh
# ============================================================================
set -u
cd "$(dirname "$0")/.."

STUBS=tools/stubs
INC="-I $STUBS -I lib/nmea -I lib/panic -I lib/buzzer -I lib/link -I lib/ranging"
COMUN="-fsyntax-only -std=gnu++11 -Wall -Wextra"
fallos=0

comprobar() {   # comprobar <descripción> <archivo> [flags...]
  local desc="$1"; shift
  local archivo="$1"; shift
  if g++ $COMUN $INC "$@" "$archivo" 2>/tmp/geo-sintaxis.log; then
    echo "  OK    $desc"
  else
    echo "  FALLA $desc"
    sed 's/^/        /' /tmp/geo-sintaxis.log
    fallos=$((fallos + 1))
  fi
}

echo "Comprobando el firmware de la DevKit (src/main.cpp):"
comprobar "devkit_v1 (perifericos integrados)" src/main.cpp
comprobar "devkit_llavero (boton+zumbador+bateria+GPS+posicion en alerta)" src/main.cpp \
    -D PIN_BUTTON_CFG=4 -D PIN_BUZZER_CFG=25 -D PIN_VBAT_CFG=34 \
    -D GPS_UART_ENABLED=1 -D ALERT_WITH_POSITION=1
comprobar "boton cableado al reves (BUTTON_ACTIVE_HIGH)" src/main.cpp \
    -D PIN_BUTTON_CFG=5 -D BUTTON_ACTIVE_HIGH=1

echo "Comprobando el firmware LoRa (src/lora/main_lora.cpp):"
comprobar "heltec_tag (llavero)" src/lora/main_lora.cpp -D LORA_ROLE=1 -D PIN_BUZZER_CFG=25
comprobar "heltec_finder (buscador)" src/lora/main_lora.cpp -D LORA_ROLE=2 -D PIN_BUZZER_CFG=25
comprobar "heltec_tag con GPS cableado" src/lora/main_lora.cpp -D LORA_ROLE=1 -D GPS_UART_ENABLED=1

echo
if [ "$fallos" -eq 0 ]; then
  echo "Todo compila. (Esto NO sustituye a 'pio test -e native' ni a la placa.)"
else
  echo "$fallos comprobacion(es) con errores."
fi
exit "$fallos"

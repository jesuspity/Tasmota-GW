# Tasmota GW — Projekte+ (fork do Tasmota)

Firmware dos gateways Zigbee **Projekte+** (ESP32 + coordenador EFR32/EZSP), derivado do [Tasmota oficial](https://github.com/arendst/Tasmota).
Esta branch traz a correção do erro **`ZbState Code 81` (ASH "ACK timeout")**, que derrubava o Zigbee quando a internet caía com o Wi-Fi ainda conectado.

## Firmware pronto (atualização OTA)

A última versão pública fica sempre no mesmo endereço:

```
https://github.com/jesuspity/Tasmota-GW/releases/latest/download/tasmota32-projekte-gw.bin
```

No console do gateway: `OtaUrl <endereço acima>` e depois `Upgrade 1`. O gateway mantém o Wi-Fi, o MQTT e a rede Zigbee já gravados.
Esse `.bin` público é compilado **sem senhas** (ambiente `tasmota32-projekte-gw-publico`); a partir da v0.2 ele já vem com esse `OtaUrl`.

> O conteúdo original do README do Tasmota continua logo abaixo da seção Projekte+.

## Rastreabilidade

| Item | Valor |
|---|---|
| Código-base oficial | `arendst/Tasmota` commit [`1c2173927cdf1be61759bcc3b064b9511a18e5d7`](https://github.com/arendst/Tasmota/commit/1c2173927cdf1be61759bcc3b064b9511a18e5d7) — "Update RELEASENOTES.md", 16/10/2026 (versão 16.0.0 pré-lançamento) |
| Branch desta correção | `projekte/zigbee-rx-task` |
| Versão gerada | `16.0.0(Projekte+ GW v0.2)` (tag `projekte-gw-v0.2`) |
| Ambiente de compilação | `tasmota32-projekte-gw` (privado, com senhas) e `tasmota32-projekte-gw-publico` (sem senhas) — [projekte/platformio_override.ini](projekte/platformio_override.ini), PlatformIO 6.2.0, Core 3.3.12 / IDF 5.5.5 |
| Hardware validado | ESP32-D0WD-V3 (4 MB, layout safeboot) + EFR32 EZSP 6.7.10.0 protocolo 8 — gateway de teste `PXXX_GW02_TEST` |
| Data da validação | v0.1: 03/11/2026 (cabo do modem retirado); v0.2: 05/11/2026 (bateria automática de 2 h) |
| Relatório completo | [projekte/docs/RELATORIO_CORRECAO_CODE81_2026-11-03.md](projekte/docs/RELATORIO_CORRECAO_CODE81_2026-11-03.md) |
| Patch isolado | [projekte/docs/patch_zigbee_rx_task.diff](projekte/docs/patch_zigbee_rx_task.diff) (aplicável com `git apply` em outras versões) |
| Explicação ilustrada (PDF) | [projekte/docs/Correcao_Zigbee_Code81_ProjektePlus.pdf](projekte/docs/Correcao_Zigbee_Code81_ProjektePlus.pdf) |

Para ver todas as diferenças em relação ao original:
```bash
git diff 1c2173927cdf1be61759bcc3b064b9511a18e5d7 -- tasmota/
```

## Comparação com o código original

### Arquivos alterados

| Arquivo | Original | Projekte+ |
|---|---|---|
| `tasmota/tasmota_xdrv_driver/xdrv_23_zigbee_9_serial.ino` | Leitura da serial Zigbee e envio de ACK **dentro do loop principal** | **Tarefa FreeRTOS `ZbRx`** lê a serial e envia o ACK; loop principal processa a fila (+237 / −25 linhas) |
| `tasmota/tasmota_xdrv_driver/xdrv_23_zigbee_A_impl.ino` | — | Pausa a tarefa durante atualização do firmware da EFR32 (+3 linhas) |
| `projekte/platformio_override.ini` (novo) | — | Ambiente enxuto `tasmota32-projekte-gw` (base Zigbee Bridge Pro, partição 2880k) |
| `projekte/user_config_override.h` (novo) | — | Configuração Projekte+ **sem senhas** (marcadores `SUA_SENHA_*`); `OTA_URL` próprio; chave `PROJEKTE_PUBLIC_BUILD` |
| `projekte/tools/simulador_queda.py` (novo) | — | Gravador de log via rede local + simulador de queda de MQTT |

Nenhum outro arquivo do Tasmota foi modificado.

### Comportamento

| Situação | Tasmota original | Tasmota GW Projekte+ |
|---|---|---|
| Internet cai, Wi-Fi conectado | MQTT/TLS tenta reconectar e **trava o loop principal 5–40 s** | Igual (o MQTT não foi alterado) |
| Mensagem da EFR32 durante o travamento | Fica sem ACK; após as retransmissões a EFR32 envia ASH ERROR `0x51` | ACK enviado em ≤ 5 ms pela tarefa `ZbRx` |
| Resultado | `{"ZbState":{"Status":99,"Message":"Failed state","Error":"ACK timeout","Code":81}}` + reinício | Zigbee continua; log `ZIG: Main loop was busy, N frames acknowledged by RX task, max wait X ms` |
| Teste real (firmware antigo 14.5.0) | Travou 10 s → Code 81 em 0,3 s | — |
| Teste real (este firmware) | — | Travou 22 s e 38 s → **0 ocorrências de Code 81** |

### Trecho principal — antes e depois

**Original** (`ZigbeeInputLoop`, loop principal): lê os bytes, monta o quadro e só então envia o ACK. Se o loop estiver travado, nada acontece.
```cpp
// xdrv_23_zigbee_8/9 (original) — chamado só pelo FUNC_LOOP
while (ZigbeeSerial->available()) { ... monta quadro ... }
...
ZigbeeProcessInputRaw(ezsp_buffer);          // dentro dela:
    EZSP_Serial.from_ack = ((control_byte >> 4) + 1) & 0x07;
    ZigbeeEZSPSendRaw(&ack_byte, 1, false);  // ACK enviado SÓ quando o loop roda
```

**Projekte+**: uma tarefa de prioridade 5 (o loop principal tem prioridade 1) roda a cada 5 ms mesmo com o loop travado.
```cpp
void ZigbeeRxTask(void *) {
  while (true) {
    ZigbeeSerialLock();
    while ((frame = ZigbeeEZSPReadFrame(true)) != nullptr) {
      if (xQueueSend(ZbRx.queue, &item, 0) == pdTRUE) {
        ZigbeeRxTaskAck(*frame);             // ACK imediato para a EFR32
      } else { delete frame; ZbRx.dropped++; } // fila cheia: EFR32 retransmite
    }
    ZigbeeSerialUnlock();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
// Loop principal: 1 quadro por ciclo (a máquina de estados precisa ver cada resposta)
if (xQueueReceive(ZbRx.queue, &item, 0) == pdTRUE) {
  zigbee_frame_preacked = true;              // não repetir o ACK
  ZigbeeEZSPProcessFrame(*item.frame);
  ...
}
```

### Parâmetros (podem ser alterados no `user_config_override.h`)

| Define | Padrão | Função |
|---|---|---|
| `ZIGBEE_EZSP_NO_RX_TASK` | não definido | Se definido, volta exatamente ao comportamento original |
| `ZIGBEE_RX_TASK_PRIORITY` | `5` | Prioridade FreeRTOS da tarefa (loop principal = 1) |
| `ZIGBEE_RX_TASK_PERIOD_MS` | `5` | Intervalo de leitura da serial (buffer de 256 bytes ≈ 22 ms a 115200 bps) |
| `PROJEKTE_PUBLIC_BUILD` | não definido | Compila sem Wi-Fi e sem usuário/senha MQTT (versão publicável) |

### Proteções da v0.2 na tarefa `ZbRx`

| Item | Situação tratada | Como |
|---|---|---|
| Controle de fluxo | Fila quase cheia (loop travado com muito tráfego Zigbee) | ACK com o bit ASH **nRdy** a partir de 48 quadros na fila; liberado abaixo de 16 |
| Sequência após descarte | Fila cheia descartava o quadro *n* e o ACK de *n+1* confirmava *n* sem ele ter sido processado | Depois de um descarte, só o quadro esperado é aceito; a EFR32 retransmite a partir dele |
| Memória | Falta de memória ao copiar o quadro | Abaixo de 8 KB livres o quadro não é confirmado e a EFR32 retransmite |
| Diagnóstico | — | Log `RX task: dropped, bad crc, time-out, out of seq, nRdy, stack free` |
| Código original | `millis()` sem proteção na virada (~49 dias); ponteiro não zerado com GPIO inválido | `TimePassedSince()`; `ZigbeeSerial = nullptr` |

## Como compilar e gravar

```bash
cp projekte/user_config_override.h tasmota/user_config_override.h   # e preencha as senhas
cp projekte/platformio_override.ini .
pio run -e tasmota32-projekte-gw            # privado, com senhas (gravação inicial)
pio run -e tasmota32-projekte-gw-publico    # público, sem senhas (OTA / GitHub Release)
```
Gravação no gateway (layout safeboot): console `Restart 3` → página **Atualizar Firmware** → enviar o `.bin`. Detalhes no relatório.

> **Segurança:** o binário do ambiente privado contém as senhas definidas no `user_config_override.h`. Publique só o `.bin` do ambiente `-publico`.

## Histórico Projekte+

| Data | Versão | Alteração |
|---|---|---|
| 03/11/2026 | 16.0.0 Projekte+ GW v0.1 | Tarefa `ZbRx` (ACK imediato da serial EZSP); ambiente enxuto `tasmota32-projekte-gw`; ferramenta de log/simulação; relatório e PDF |
| 05/11/2026 | 16.0.0 Projekte+ GW v0.2 | `OTA_URL` próprio (GitHub Release); versão pública sem senhas; controle de fluxo nRdy, sequência após descarte e checagem de memória na `ZbRx`; correções `millis()` e ponteiro do original |

---

![Tasmota logo](/tools/logo/TASMOTA_FullLogo_Vector.svg#gh-light-mode-only)![Tasmota logo](/tools/logo/TASMOTA_FullLogo_Vector_White.svg#gh-dark-mode-only)

Alternative firmware for [ESP8266](https://en.wikipedia.org/wiki/ESP8266) and [ESP32](https://en.wikipedia.org/wiki/ESP32) based devices with **easy configuration using webUI, OTA updates, automation using timers or rules, expandability and entirely local control over MQTT, HTTP, Serial or KNX**.
_Written for PlatformIO._

[![GitHub version](https://img.shields.io/github/release/arendst/Tasmota.svg)](http://ota.tasmota.com/tasmota/release)
[![GitHub download](https://img.shields.io/github/downloads/arendst/Tasmota/total.svg)](https://github.com/arendst/Tasmota/releases/latest)
[![License](https://img.shields.io/github/license/arendst/Tasmota.svg)](LICENSE.txt)
[![Discord](https://img.shields.io/discord/479389167382691863.svg?logo=discord&logoColor=white&color=5865F2&label=Discord)](https://discord.gg/Ks2Kzd4)

<hr></hr>

**In light of current events we like to support the people behind _PlatformIO Project_, especially Ivan Kravets, and wish them the strength to help stop the war. See [platformio-is-ukrainian-project-please-help-us-stop-the-war](https://community.platformio.org/t/platformio-is-ukrainian-project-please-help-us-stop-the-war/26330) for what you can do.**

<hr></hr>

## Why Tasmota?

Tasmota is open-source firmware born for the **ESP8266**/**ESP8285**, now covering the whole ESP32 family: **ESP32**, **ESP32-S2**, **ESP32-S3**, **ESP32-C2**, **ESP32-C3**, **ESP32-C5**, **ESP32-C6**, **ESP32-C61**, **ESP32-P4**, with PSRAM and USB/CDC support where available.

It talks MQTT (v3.1.1 or v5) to your own broker or a cloud one over TLS ([AWS IoT Core](https://tasmota.github.io/docs/AWS-IoT/) and [Azure IoT Hub](https://tasmota.github.io/docs/Azure-IoT-Hub/)), or runs entirely standalone with no server at all — your choice.

Here's a tour of what it can do:

| Area | What you get |
|---|---|
| **Smart home integrations** | Plugs into [Home Assistant](https://tasmota.github.io/docs/Home-Assistant/), [Domoticz](https://tasmota.github.io/docs/Domoticz/), [openHAB](https://tasmota.github.io/docs/openHAB/), [Node-RED](https://tasmota.github.io/docs/NodeRed/), [KNX](https://tasmota.github.io/docs/KNX/) and more via [MQTT](https://tasmota.github.io/docs/MQTT/), local or cloud ([AWS IoT Core](https://tasmota.github.io/docs/AWS-IoT/) and [Azure IoT Hub](https://tasmota.github.io/docs/Azure-IoT-Hub/)). See [Smart Home Integrations](https://tasmota.github.io/docs/Integrations/). |
| **Matter protocol** | Runs natively on ESP32, no MQTT, hub or companion app required. Pairs directly with Apple Home, Google Home, Amazon Alexa and Home Assistant. An ESP32 can also bridge your existing ESP8266 devices into Matter. See [Matter](https://tasmota.github.io/docs/Matter/). |
| **Sensors & peripherals** | 150+ supported sensors and peripherals over I²C, SPI, 1-Wire and analog, most auto-detected once wired: temperature/humidity ([DHT11](https://tasmota.github.io/docs/DHT11/)/DHT22, [DS18x20](https://tasmota.github.io/docs/DS18x20/), [BME280](https://tasmota.github.io/docs/BME280/), [SHT30](https://tasmota.github.io/docs/SHT30/)), CO2 ([MH-Z19](https://tasmota.github.io/docs/MH-Z19B/), [SCD4x](https://tasmota.github.io/docs/SCD4x/)), air quality/dust (PMS5003, SPS30), energy monitoring (INA219, Eastron SDM), motion (PIR, mmWave radar), light ([BH1750](https://tasmota.github.io/docs/BH1750/)), and many more. See [Peripherals](https://tasmota.github.io/docs/Peripherals/) and the [device database](https://templates.blakadder.com/). |
| **Home appliances** | Drives [shutters and blinds](https://tasmota.github.io/docs/Blinds-and-Shutters/) (pulse, stepper or servo motors), [thermostat](https://tasmota.github.io/docs/Thermostat/) control for heating/cooling, and reads [smart meter interfaces](https://tasmota.github.io/docs/Smart-Meter-Interface/) (SML, OBIS, Modbus meters) for energy and utility data. |
| **Lights** | Dims, color-temperature-tunes or full-color controls [light bulbs and strips](https://tasmota.github.io/docs/Lights/): single-channel dimmers, CT, RGB, RGBW and RGBCW, plus addressable LED strips (WS2812, SK6812...). |
| **Protocol bridging** | Speaks [Infrared](https://tasmota.github.io/docs/Tasmota-IR/) (send & receive), [RF](https://tasmota.github.io/docs/RF-Protocol/), [DALI](https://tasmota.github.io/docs/DALI/), [Modbus](https://tasmota.github.io/docs/Modbus-Bridge/), RS-485, [OpenTherm](https://tasmota.github.io/docs/OpenTherm/), [TWAI](https://tasmota.github.io/docs/TWAI/)/CAN, [LoRa/LoRaWan](https://tasmota.github.io/docs/LoRa-and-LoRaWan-Bridge/), [HDMI-CEC](https://tasmota.github.io/docs/HDMI_CEC/), [Telegram](https://tasmota.github.io/docs/Telegram/) and SMTP email, plus a generic Arduino MCU bridge. |
| **Zigbee & Bluetooth** | Turns an ESP32 into a [Zigbee](https://tasmota.github.io/docs/Zigbee/) coordinator or a [BLE gateway](https://tasmota.github.io/docs/Bluetooth/) (including BTHome), bridging devices without a separate hub. |
| **Displays & touch UI** | Drives [character, graphic and e-paper displays](https://tasmota.github.io/docs/Displays/), and on ESP32 renders rich, animated touchscreen UIs with [LVGL](https://tasmota.github.io/docs/LVGL/) — resistive or capacitive touch, SPI panels. [HASPmota](https://tasmota.github.io/docs/HASPmota/) builds full UIs from simple JSON templates, no LVGL or Berry code needed. |
| **Automation & scripting** | [Rules](https://tasmota.github.io/docs/Rules/) and [Timers](https://tasmota.github.io/docs/Timers/) handle on-device automation; the [Berry](https://tasmota.github.io/docs/Berry/) scripting language gives full programmatic control — it's also what powers LVGL, HASPmota and the animation engine. |
| **Networking** | Wired Ethernet, [IPv6](https://tasmota.github.io/docs/IPv6/), [Wi-Fi range extender](https://tasmota.github.io/docs/Range-Extender/) mode, a built-in [WireGuard VPN](https://tasmota.github.io/docs/Wireguard/) client for secure remote access, and [TasMesh](https://github.com/arendst/Tasmota/blob/development/info/xdrv_57_tasmesh.md) for encrypted ESP-NOW mesh links to battery-powered nodes. |
| **LED animation** | A dedicated DSL-based engine animates addressable LED strips (WS2812, SK6812...) with effects like pulse, breathe, comet and twinkle. Design and preview animations in the [online browser emulator](https://tasmota.github.io/docs/Tasmota-Berry-emulator/index.html) before flashing. |
| **Audio** | [I2S audio](https://tasmota.github.io/docs/I2S-Audio_ESP32/) support adds microphone input and speaker/DAC output, including MP3/AAC/OPUS streaming. |
| **And more** | [TuyaMCU](https://tasmota.github.io/docs/TuyaMCU/) support for Tuya Wi-Fi/MCU devices, [Shelly Gen2 emulation](https://tasmota.github.io/docs/Shelly-Emulation/), [deep](https://tasmota.github.io/docs/DeepSleep/)/[dynamic](https://tasmota.github.io/docs/Dynamic-Sleep/) sleep for battery devices, [Device Groups](https://tasmota.github.io/docs/Device-Groups/) for keeping devices in sync, [ArtNet DMX](https://tasmota.github.io/docs/ArtNet/), [TensorFlow Lite](https://tasmota.github.io/docs/TFL/) and [ULP coprocessor](https://tasmota.github.io/docs/ULP/) scripting on ESP32, a [file system](https://tasmota.github.io/docs/UFS/) with optional SD card storage. |

Not every feature fits in the precompiled release binaries due to flash size limits — some require [compiling your own build](https://tasmota.github.io/docs/Compile-your-build/). See the full [documentation](https://tasmota.github.io/docs) for details.

<hr></hr>

## Easy install

Easy initial installation of Tasmota can be performed using the [Tasmota WebInstaller](https://tasmota.github.io/install/).

If you like **Tasmota**, give it a star, or fork it and contribute!

[![GitHub stars](https://img.shields.io/github/stars/arendst/Tasmota.svg?style=social&label=Star)](https://github.com/arendst/Tasmota/stargazers)
[![GitHub forks](https://img.shields.io/github/forks/arendst/Tasmota.svg?style=social&label=Fork)](https://github.com/arendst/Tasmota/network)
[![donate](https://img.shields.io/badge/donate-PayPal-blue.svg)](https://paypal.me/tasmota)

See [RELEASENOTES.md](https://github.com/arendst/Tasmota/blob/master/RELEASENOTES.md) for release information.

Firmware binaries can be downloaded from http://ota.tasmota.com/tasmota/release/ or http://ota.tasmota.com/tasmota32/release/ for ESP32 binaries.

## Development

[![Dev Version](https://img.shields.io/badge/development%20version-v16.0.x.x-blue.svg)](https://github.com/arendst/Tasmota)
[![Download Dev](https://img.shields.io/badge/download-development-yellow.svg)](http://ota.tasmota.com/tasmota/)
[![Tasmota CI](https://github.com/arendst/Tasmota/actions/workflows/build_all_the_things.yml/badge.svg)](https://github.com/arendst/Tasmota/actions/workflows/build_all_the_things.yml)
[![Build_development](https://github.com/arendst/Tasmota/actions/workflows/Tasmota_build_devel.yml/badge.svg)](https://github.com/arendst/Tasmota/actions/workflows/Tasmota_build_devel.yml)

See [CHANGELOG.md](CHANGELOG.md) for detailed change information.

Unless your Tasmota powered device exhibits a problem or lacks a feature that you need, leave your device alone - it works so don’t make unnecessary changes! If the release version (i.e., the master branch) exhibits unexpected behaviour for your device and configuration, you should upgrade to the latest development version instead to see if your problem is resolved as some bugs in previous releases or development builds may already have been resolved.

Every commit made to the development branch, which is compiling successfully, will post new binary files at http://ota.tasmota.com/tasmota/ (this web address can be used for OTA updates too). It is important to note that these binaries are based on the current development codebase. These commits are tested as much as is possible and are typically quite stable. However, it is infeasible to test on the hundreds of different types of devices with all the available configuration options permitted.

Note that there is a chance, as with any upgrade, that the device may not function as expected. You must always account for the possibility that you may need to flash the device via the serial programming interface if the OTA upgrade fails. Even with the master release, you should always attempt to test the device or a similar prototype before upgrading a device which is in production or is hard to reach. And, as always, make a backup of the device configuration before beginning any firmware update.

## Disclaimer

:warning: **DANGER OF ELECTROCUTION** :warning:

If your device connects to mains electricity (AC power) there is danger of electrocution if not installed properly. If you don't know how to install it, please call an electrician (***Beware:*** certain countries prohibit installation without a licensed electrician present). Remember: _**SAFETY FIRST**_. It is not worth the risk to yourself, your family and your home if you don't know exactly what you are doing. Never tinker or try to flash a device using the serial programming interface while it is connected to MAINS ELECTRICITY (AC power).

We don't take any responsibility nor liability for using this software nor for the installation or any tips, advice, videos, etc. given by any member of this site or any related site.

## Note

Please do not ask to add new devices unless it requires additional code for new features. If the device is not listed as a module, try using [Templates](https://tasmota.github.io/docs/Templates) first. If it is not listed in the [Tasmota Device Templates Repository](http://templates.blakadder.com) create your own [Template](https://tasmota.github.io/docs/Templates#creating-your-template).

## Quick Install
Download one of the released binaries from http://ota.tasmota.com/tasmota/release/ or http://ota.tasmota.com/tasmota32/release/ and flash it to your hardware [using our installation guide](https://tasmota.github.io/docs/Getting-Started).

## Important User Compilation Information
If you want to compile Tasmota yourself keep in mind the following:

- For ESP8285 based devices only Flash Mode **DOUT** is supported. Do not use Flash Mode DIO / QIO / QOUT as it might seem to brick your device.
- For ESP8285 based devices Tasmota uses a 1M linker script WITHOUT spiffs **1M (no SPIFFS)** for optimal code space.
- To make compile time changes to Tasmota use the `user_config_override.h` file. It assures keeping your custom settings when you download and compile a new version. You have to make a copy from the provided `user_config_override_sample.h` file and add your setting overrides.

## Configuration Information

Please refer to the installation and configuration articles in our [documentation](https://tasmota.github.io/docs).

## Migration Information

See [migration path](https://tasmota.github.io/docs/Upgrading#migration-path) for instructions how to migrate to a major version.

**Do not upgrade from minimal to minimal version. It will most likely fail at some point and will require flashing via serial.** If you do have to use minimal versions, always OTA to a full version of the same release before applying next minimal version.

Pay attention to the following version breaks due to dynamic settings updates:

1. Migrate to **Sonoff-Tasmota 3.9.x**
2. Migrate to **Sonoff-Tasmota 4.x**
3. Migrate to **Sonoff-Tasmota 5.14**
4. Migrate to **Sonoff-Tasmota 6.7.1** (http://ota.tasmota.com/tasmota/release_6.7.1/sonoff.bin) - NOTICE underscore as a dash is not supported in older versions
5. Migrate to **Tasmota 7.2.0** (http://ota.tasmota.com/tasmota/release-7.2.0/tasmota.bin)

--- Major change in parameter storage layout ---

6. Migrate to **Tasmota 8.5.1** (http://ota.tasmota.com/tasmota/release-8.5.1/tasmota.bin)

--- Major change in internal GPIO function representation ---

7. Migrate to **Tasmota 9.1** (http://ota.tasmota.com/tasmota/release-9.1.0/tasmota.bin.gz)
8. Upgrade to **latest release** (http://ota.tasmota.com/tasmota/release/tasmota.bin.gz)

While fallback or downgrading is common practice it was never supported due to Settings additions or changes in newer releases. Starting with release **v9.1.0 Imogen** the internal GPIO function representation has changed in such a way that fallback is only possible to the latest GPIO configuration before installing **v9.1.0**.

## Support Information

<img src="https://user-images.githubusercontent.com/5904370/68332933-e6e5a600-00d7-11ea-885d-50395f7239a1.png" width=150 align="right" />

For a database of supported devices see [Tasmota Device Templates Repository](https://templates.blakadder.com)

If you're looking for support on **Tasmota** there are some options available:

### Documentation

* [Documentation Site](https://tasmota.github.io/docs): For information on how to flash Tasmota, configure, use and expand it
* [FAQ and Troubleshooting](https://tasmota.github.io/docs/FAQ/): For information on common problems and solutions.
* [Commands Information](https://tasmota.github.io/docs/Commands): For information on all the commands supported by Tasmota.

### Support's Community

* [Tasmota Discussions](https://github.com/arendst/Tasmota/discussions): For Tasmota usage questions, Feature Requests and Projects.
* [Tasmota Users Chat](https://discord.gg/Ks2Kzd4): For support, troubleshooting and general questions. You have better chances to get fast answers from members of the Tasmota Community.
* [Search in Issues](https://github.com/arendst/Tasmota/issues): You might find an answer to your question by searching current or closed issues.
* [Software Problem Report](https://github.com/arendst/Tasmota/issues/new?template=Bug_report.md): For reporting problems of Tasmota Software.

### Unofficial Community Resources
* [Tasmota-DE](https://t.me/TasmotaDE): A German-language Telegram group related to Tasmota.

## Contribute

You can contribute to Tasmota by
- Providing Pull Requests (Features, Proof of Concepts, Language files or Fixes)
- Testing new released features and report issues
- Donating to acquire hardware for testing and implementing or out of gratitude
- Contributing missing [documentation](https://tasmota.github.io/docs) for features and devices

[![donate](https://img.shields.io/badge/donate-PayPal-blue.svg)](https://paypal.me/tasmota)

## Credits

People helping to keep the show on the road:
- Sfromis providing extensive user support
- Barbudor providing user support and code fixes and additions
- David Lang providing initial issue resolution and code optimizations
- Heiko Krupp for his IRSend, HTU21, SI70xx and Wemo/Hue emulation drivers
- Wiktor Schmidt for Travis CI implementation
- Thom Dietrich for PlatformIO optimizations
- Marinus van den Broek for his EspEasy groundwork
- Pete Ba for more user friendly energy monitor calibration
- Lobradov providing compile optimization tips
- Flexiti for his initial timer implementation
- reloxx13 for his [TasmoAdmin](https://github.com/reloxx13/TasmoAdmin) management tool
- Joachim Banzhaf for his TSL2561 library and driver
- Andre Thomas for providing many drivers
- Gijs Noorlander for his MHZ19, SenseAir and updated PubSubClient drivers
- Erik Montnemery for his HomeAssistant Discovery concept and many code tuning tips
- Federico Leoni for continued HomeAssistant Discovery support
- Aidan Mountford for his HSB support
- Daniel Ztolnai for his Serial Bridge implementation
- Gerhard Mutz for multiple sensor & display drivers, Sunrise/Sunset, and scripting
- Nuno Ferreira for his HC-SR04 driver
- Adrian Scillato for his (security)fixes and implementing and maintaining KNX
- Gennaro Tortone for implementing and maintaining Eastron drivers
- Raymond Mouthaan for managing Wemos Wiki information
- Norbert Richter for his [decode-config.py](https://github.com/tasmota/decode-config) tool
- Joel Stein, digiblur and Shantur Rathore for their Tuya research and driver
- Frogmore42 for providing many issue answers
- Jason2866 for platformio support and providing many issue answers
- Blakadder for managing the document site and providing template management
- Stephan Hadinger for refactoring light driver, enhancing HueEmulation, LVGL, Zigbee and Berry support
- tmo for designing the official Tasmota logo
- Stefan Bode for his Shutter and Deep sleep drivers
- Jacek Ziółkowski for his [TDM](https://github.com/jziolkowski/tdm) management tool and [Tasmotizer](https://github.com/tasmota/tasmotizer) flashing tool
- Christian Staars for NRF24L01 and HM-10 Bluetooth sensor support
- Paul Diem for UDP Group communication support
- Jörg Schüler-Maroldt for his initial ESP32 port
- Javier Arigita for his thermostat driver
- Simon Hailes for ESP32 Bluetooth extensions
- Many more providing Tips, Wips, Pocs, PRs and Donations

## License

This program is licensed under GPL-3.0-only

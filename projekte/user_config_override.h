// Projekte+ GW: copia publica SEM SENHAS. Substitua os marcadores SUA_SENHA_* / SEU_USUARIO_MQTT antes de compilar.
// Copie este arquivo para tasmota/user_config_override.h

#ifndef _USER_CONFIG_OVERRIDE_H_
#define _USER_CONFIG_OVERRIDE_H_

// #define USE_PROJEKTE_DISCOVERY


// ======================================================
// BUILD COMPLETO - GATEWAY PROJEKTE+ + MQTT TLS
// Projeto: Gateway / Zigbee / Ethernet / Berry / Filesystem
// Uso: ICDI_GW01_AC01
// ======================================================
//
// Cole este conteúdo no campo Custom Parameters do TasmoCompiler
// ou no arquivo user_config_override.h.
//
// Observação:
// - Este build é pesado e deve ser usado em ESP32.
// - Berry, Matter, PSRAM, SD Card/LittleFS e Zigbee não são
//   recomendados para ESP8266 por limitação de flash/RAM.
// ======================================================


// ======================================================
// 1) IDENTIFICAÇÃO DO DISPOSITIVO
// ======================================================

#define STORE_ID  "Loja267"             // Loja/unidade usada no FullTopic MQTT

#define DEVICE_ID "I267_GW01_FARM"      // Topic, DeviceName e FriendlyName I007-GW01-FARM
#define UNIQUE_ID DEVICE_ID "_%06X"     // MqttClient único usando final do MAC


// ======================================================
// 2) WI-FI PADRÃO
// ======================================================

#undef STA_SSID1
#define STA_SSID1 "IoT"            // Rede Wi-Fi principal

#undef STA_PASS1
#define STA_PASS1 "SUA_SENHA_WIFI_1"         // Senha da rede Wi-Fi principal

#undef STA_SSID2
#define STA_SSID2 "projekte"                 // Rede Wi-Fi secundária/fallback

#undef STA_PASS2
#define STA_PASS2 "SUA_SENHA_WIFI_2"         // Senha da rede Wi-Fi secundária


// ======================================================
// 3) MQTT PADRÃO
// ======================================================
//
// MQTT remoto com TLS:
// Host: mqtt.projekteplus.com
// Porta: 8883
//
// SetOption103 1 será aplicado no USER_BACKLOG
// para ativar TLS no runtime.
// ======================================================

#undef MQTT_HOST
#define MQTT_HOST "mqtt.projekteplus.com"

#undef MQTT_PORT
#define MQTT_PORT 8883

#undef MQTT_USER
#define MQTT_USER "SEU_USUARIO_MQTT"

#undef MQTT_PASS
#define MQTT_PASS "SUA_SENHA_MQTT"


// ======================================================
// 4) RECURSOS DO FIRMWARE
// ======================================================


// ======================================================
// 4.1) RECURSOS ATIVOS
// ======================================================

// ---------- IDENTIFICAÇÃO DA IMAGEM ----------
#ifdef CODE_IMAGE_STR
  #undef CODE_IMAGE_STR
#endif
#define CODE_IMAGE_STR "Projekte+ GW v0.1"


// ---------- IDIOMA CUSTOMIZADO ----------
#ifdef MY_LANGUAGE
  #undef MY_LANGUAGE
#endif
//#define MY_LANGUAGE pt_BR


// ---------- TASMOTA DISCOVERY / mDNS / HOME ASSISTANT ----------
// USE_DISCOVERY mantém suporte de descoberta/mDNS.
// USE_HOME_ASSISTANT mantém integração com Home Assistant.
#ifdef USE_DISCOVERY
  #undef USE_DISCOVERY
#endif
#define USE_DISCOVERY

#ifdef USE_TASMOTA_DISCOVERY
  #undef USE_TASMOTA_DISCOVERY
#endif
#define USE_TASMOTA_DISCOVERY

#ifdef USE_HOME_ASSISTANT
  #undef USE_HOME_ASSISTANT
#endif
#define USE_HOME_ASSISTANT


// ---------- MATTER PROTOCOL ----------
// Recurso para builds ESP32/Tasmota32.
#undef USE_MATTER_DEVICE
#ifdef USE_MATTER_DEVICE
  #undef USE_MATTER_DEVICE
#endif
//#define USE_MATTER_DEVICE


// ---------- DOMOTICZ ----------
#ifdef USE_DOMOTICZ
  #undef USE_DOMOTICZ
#endif
//#define USE_DOMOTICZ


// ---------- MQTT TLS ----------
// Compila suporte a MQTT over TLS.
// No runtime será ativado com SetOption103 1.
#ifdef USE_MQTT_TLS
  #undef USE_MQTT_TLS
#endif
#define USE_MQTT_TLS

#ifdef MQTT_TLS_ENABLED
  #undef MQTT_TLS_ENABLED
#endif
#define MQTT_TLS_ENABLED true


// ---------- ETHERNET ----------
#ifdef USE_ETHERNET
  #undef USE_ETHERNET
#endif
#define USE_ETHERNET


// ---------- WEBSERVER ----------
#ifdef USE_WEBSERVER
  #undef USE_WEBSERVER
#endif
#define USE_WEBSERVER


// ---------- BERRY SCRIPTING ----------
#ifdef USE_BERRY
  #undef USE_BERRY
#endif
#define USE_BERRY

#ifdef USE_BERRY_PSRAM
  #undef USE_BERRY_PSRAM
#endif
#define USE_BERRY_PSRAM


// ---------- Stack ----------
#ifdef SET_ESP32_STACK_SIZE
  #undef SET_ESP32_STACK_SIZE
#endif
#define SET_ESP32_STACK_SIZE (24 * 1024)


// ---------- RULES / EXPRESSIONS / IF / TIMERS ----------
#ifdef USE_RULES
  #undef USE_RULES
#endif
#define USE_RULES

#ifdef USE_EXPRESSION
  #undef USE_EXPRESSION
#endif
#define USE_EXPRESSION

#ifdef SUPPORT_IF_STATEMENT
  #undef SUPPORT_IF_STATEMENT
#endif
#define SUPPORT_IF_STATEMENT

#ifdef USE_TIMERS
  #undef USE_TIMERS
#endif
#define USE_TIMERS


// ---------- FILESYSTEM - UFS / LITTLEFS / SD CARD ----------
// UFS/LittleFS permite usar arquivos internos.
// SD Card exige SPI e GPIOs compatíveis no hardware/template.
#ifdef USE_UFILESYS
  #undef USE_UFILESYS
#endif
#define USE_UFILESYS

#ifdef USE_SDCARD
  #undef USE_SDCARD
#endif

#ifdef GUI_TRASH_FILE
  #undef GUI_TRASH_FILE
#endif
#define GUI_TRASH_FILE

#ifdef GUI_EDIT_FILE
  #undef GUI_EDIT_FILE
#endif
#define GUI_EDIT_FILE


// ---------- BARRAMENTOS ----------
#ifdef USE_I2C
  #undef USE_I2C
#endif
//#define USE_I2C

#ifdef USE_SPI
  #undef USE_SPI
#endif
//#define USE_SPI


// ---------- LED ENDEREÇÁVEL ----------
#ifdef USE_WS2812
  #undef USE_WS2812
#endif
// #define USE_WS2812
// Mantido desativado porque você enviou apenas o #undef.
// Ative se o gateway realmente tiver LED WS2812/NeoPixel.


// ---------- ZIGBEE ----------
#ifdef USE_ZIGBEE
  #undef USE_ZIGBEE
#endif
#define USE_ZIGBEE

// Note: USE_ZIGBEE_ZNP and USE_ZIGBEE_EZSP são mutuamente incompatíveis.

// Desabilita ZNP para evitar conflito com o chip EFR32.
#ifdef USE_ZIGBEE_ZNP
  #undef USE_ZIGBEE_ZNP
#endif

// Habilita EZSP para EFR32.
#ifdef USE_ZIGBEE_EZSP
  #undef USE_ZIGBEE_EZSP
#endif
#define USE_ZIGBEE_EZSP

// ======================================================
// ZIGBEE - PARÂMETROS PADRÃO DO BUILD
// ======================================================
//
// Observação importante:
// - Estes defines são padrões de compilação.
// - A rede Zigbee real de cada gateway deve ser definida depois,
//   via comando ZbConfig individual por gateway.
// - Não coloque PanID, ExtPanID, KeyL e KeyH fixos aqui.
// ======================================================


// ---------- CANAL ZIGBEE PADRÃO ----------
#ifdef USE_ZIGBEE_CHANNEL
  #undef USE_ZIGBEE_CHANNEL
#endif
#define USE_ZIGBEE_CHANNEL 20
// Canal Zigbee padrão do firmware.
// Pode ser alterado depois por gateway usando:
// ZbConfig {"Channel":11}


// ---------- POTÊNCIA DO RÁDIO ZIGBEE ----------
#ifdef USE_ZIGBEE_TXRADIO_DBM
  #undef USE_ZIGBEE_TXRADIO_DBM
#endif
#define USE_ZIGBEE_TXRADIO_DBM 20
// Potência do rádio Zigbee.
// Para seu gateway EFR32/ZBBridge, 20 dBm funcionou.
// Se houver muita interferência ou gateways muito próximos,
// pode testar 10~15 dBm em ambiente real.


// ---------- AGRUPAMENTO DE ATRIBUTOS ZIGBEE ----------
#ifdef USE_ZIGBEE_COALESCE_ATTR_TIMER
  #undef USE_ZIGBEE_COALESCE_ATTR_TIMER
#endif
#define USE_ZIGBEE_COALESCE_ATTR_TIMER 350
// Agrupa atributos recebidos quase ao mesmo tempo.
// Ajuda a reduzir mensagens MQTT fragmentadas.
// 350 ms é um bom valor para sensores de temperatura/umidade.


// ---------- IDENTIFICAÇÃO DO COORDENADOR ZIGBEE ----------
#ifdef USE_ZIGBEE_MODELID
  #undef USE_ZIGBEE_MODELID
#endif
#define USE_ZIGBEE_MODELID "Projekte+ Z2T"
// Nome/modelo anunciado pelo coordenador Zigbee.

#ifdef USE_ZIGBEE_MANUFACTURER
  #undef USE_ZIGBEE_MANUFACTURER
#endif
#define USE_ZIGBEE_MANUFACTURER "Projekte+"
// Fabricante anunciado pelo coordenador Zigbee.


// ======================================================
// ZBBRIDGE / EEPROM
// ======================================================
//
// No seu log apareceu:
// ZIG: ZBBridge EEPROM found at address 0x50
//
// Portanto, manter EEPROM 0x50 é correto para esse hardware.
// ======================================================

#ifdef USE_ZIGBEE_ZBBRIDGE_EEPROM
  #undef USE_ZIGBEE_ZBBRIDGE_EEPROM
#endif
#define USE_ZIGBEE_ZBBRIDGE_EEPROM 0x50
// EEPROM física do gateway Zigbee.
// Necessária no seu hardware, pois o log confirmou EEPROM em 0x50.


// ======================================================
// ZIGBEE - TAREFA DE RECEPÇÃO PRIORITÁRIA (Projekte+)
// ======================================================
//
// Correção do erro ZbState Code 81 "ACK timeout":
// quando a internet cai, a reconexão MQTT/TLS trava o loop principal
// por 5 a 10 segundos e a placa Zigbee (EFR32) fica sem confirmação (ACK).
//
// O arquivo xdrv_23_zigbee_9_serial.ino foi alterado: uma tarefa separada,
// de prioridade maior que o loop principal, lê a serial Zigbee e envia
// o ACK na hora, mesmo com o MQTT travado. Ela é ativada automaticamente
// no ESP32. Para voltar ao comportamento original, descomente a linha abaixo.
// ======================================================

// #define ZIGBEE_EZSP_NO_RX_TASK

#ifdef ZIGBEE_RX_TASK_PRIORITY
  #undef ZIGBEE_RX_TASK_PRIORITY
#endif
#define ZIGBEE_RX_TASK_PRIORITY 5
// Prioridade da tarefa Zigbee: acima do loop principal (1)
// e abaixo das tarefas de Wi-Fi/rede do ESP32.

#ifdef ZIGBEE_RX_TASK_PERIOD_MS
  #undef ZIGBEE_RX_TASK_PERIOD_MS
#endif
#define ZIGBEE_RX_TASK_PERIOD_MS 5
// A tarefa lê a serial Zigbee a cada 5 ms.


// ======================================================
// ZBBRIDGE TLS
// ======================================================
//
// Não confundir com MQTT TLS.
// MQTT TLS já é ativado com USE_MQTT_TLS + SetOption103 1.
//
// Para evitar peso desnecessário no build e reduzir chance de efeitos
// colaterais, eu deixaria USE_ZBBRIDGE_TLS desativado.
// ======================================================

#ifdef USE_ZBBRIDGE_TLS
  #undef USE_ZBBRIDGE_TLS
#endif
// #define USE_ZBBRIDGE_TLS
// Mantido desativado.
// Ative somente se você realmente precisar de recurso específico
// de TLS ligado ao ZBBridge, não ao MQTT.


// ======================================================
// 4.2) RECURSOS REMOVIDOS
// ======================================================
//
// Remoção somente do que não foi solicitado para este gateway.
// Não remova aqui os recursos ativados na seção 4.1.
// ======================================================

// ---------- EMULAÇÕES / RECURSOS NÃO USADOS ----------
#undef USE_EMULATION
#undef USE_EMULATION_HUE
#undef USE_EMULATION_WEMO
#undef USE_RC_SWITCH
#undef USE_TUYA_MCU
#undef USE_SHUTTER
#undef USE_MODBUS_BRIDGE
#undef USE_MODBUS_BRIDGE_TCP

// ---------- DISPLAYS NÃO USADOS ----------
#undef USE_DISPLAY
#undef USE_UNIVERSAL_DISPLAY
#undef USE_UNIVERSAL_TOUCH
#undef USE_DISPLAY_TM1637
#undef USE_DISPLAY_MAX7219
#undef USE_DISPLAY_MODES1TO5
#undef USE_DISPLAY_LCD
#undef USE_DISPLAY_MATRIX
#undef USE_DISPLAY_TM1650
#undef USE_DISPLAY_EPAPER_29
#undef USE_DISPLAY_EPAPER_42
#undef USE_DISPLAY_RA8876
#undef USE_DISPLAY_SEVENSEG


// ======================================================
// 5) CONFIGURAÇÃO AUTOMÁTICA NO PRIMEIRO BOOT / RESET 1
// ======================================================
//
// USER_BACKLOG executa comandos de console automaticamente.
// Não coloque a palavra "Backlog" dentro dele.
// Use somente os comandos separados por ponto e vírgula.
// ======================================================

#undef USER_BACKLOG

#define USER_BACKLOG \
  /* -------------------------------------------------- */ \
  /* PAUSA INICIAL                                      */ \
  /* -------------------------------------------------- */ \
  "Delay 2; " \
  /* Aguarda 2 segundos para o sistema estabilizar após o boot inicial */ \
  \
  /* -------------------------------------------------- */ \
  /* TEMPLATE DO HARDWARE                               */ \
  /* -------------------------------------------------- */ \
  "Template {\"NAME\":\"Gateway - Projekte+\",\"GPIO\":[0,0,3552,0,3584,0,0,0,5793,5792,320,544,5536,0,5600,0,0,0,0,5568,0,0,0,0,0,0,0,0,608,640,32,0,0,0,0,0],\"FLAG\":0,\"BASE\":1}; " \
  /* Template = define os GPIOs do Gateway - Projekte+ */ \
  /* 3552/3584 = sinais Ethernet PHY */ \
  /* 5792/5793 = barramento I2C */ \
  /* 5536/5568/5600 = sinais Zigbee/EFR32 conforme template informado */ \
  /* 608/640 = comunicação serial */ \
  /* 544 = LedLink / LED de status */ \
  /* 32 = Button1 / botão físico */ \
  "Module 0; " \
  /* Module 0 = ativa o template customizado */ \
  "Delay 2; " \
  \
    /* -------------------------------------------------- */ \
  /* ZIGBEE                                             */ \
  /* -------------------------------------------------- */ \
  /*"ZbPermitJoin 0;"*/\
  /*"ZbConfig {\"Channel\":20,\"PanID\":\"0x1A71\",\"ExtPanID\":\"0xCCCCCCCC00000011\",\"KeyL\":\"0xXXXXXXXXXXXXXXXX\",\"KeyH\":\"0xXXXXXXXXXXXXXXXX\",\"TxRadio\":20}; " */\
  /* ZbConfig = fixa a rede Zigbee do gateway */ \
  /* ZbConfig = fixa a rede Zigbee do gateway */ \
  /* Channel 11 = canal Zigbee usado pela rede */ \
  /* PanID 0x1A62 = identificador curto da rede Zigbee DEVE SER ALTERADO PARA CADA GATWAY*/ \
  /* ExtPanID 0xCCCCCCCC00000001 = identificador estendido da rede Zigbee DEVE SER ALTERADO PARA CADA GATWAY*/ \
  /* KeyL/KeyH = chave da rede Zigbee; mantenha esses valores em sigilo */ \
  /* TxRadio 20 = potência de transmissão do rádio Zigbee */ \
  "Delay 2; " \
  \
  /* -------------------------------------------------- */ \
  /* WI-FI                                              */ \
  /* -------------------------------------------------- */ \
  "SSID1 IoT; " \
  /* SSID1 = rede Wi-Fi principal */ \
  "Password1 SUA_SENHA_WIFI_1; " \
  /* Password1 = senha da rede Wi-Fi principal */ \
  "SSID2 projekte; " \
  /* SSID2 = rede Wi-Fi secundária/fallback */ \
  "Password2 SUA_SENHA_WIFI_2; " \
  /* Password2 = senha da rede Wi-Fi secundária/fallback */ \
  "Delay 2; " \
  \
  /* -------------------------------------------------- */ \
  /* MQTT                                               */ \
  /* -------------------------------------------------- */ \
  "MqttHost mqtt.projekteplus.com; " \
  /* MqttHost = endereço do broker MQTT */ \
  "MqttPort 8883; " \
  /* MqttPort 8883 = porta MQTT com TLS */ \
  "MqttUser SEU_USUARIO_MQTT; " \
  /* MqttUser = usuário de autenticação MQTT */ \
  "MqttPassword SUA_SENHA_MQTT; " \
  /* MqttPassword = senha de autenticação MQTT */ \
  "SetOption103 1; " \
  /* SetOption103 1 = ativa conexão MQTT com TLS */ \
  "SetOption132 0; " \
  /* SetOption132 0 = não usa validação TLS por fingerprint */ \
  "Delay 2; " \
  \
  /* -------------------------------------------------- */ \
  /* IDENTIFICAÇÃO MQTT / REDE                          */ \
  /* -------------------------------------------------- */ \
  "Topic " DEVICE_ID "; " \
  /* Topic = tópico MQTT principal do gateway */ \
  "FullTopic Indiana/" STORE_ID "/%topic%/%prefix%/; " \
  /* FullTopic = padrão MQTT: Indiana/LojaXXX/DEVICE/cmnd|stat|tele/ */ \
  "DeviceName " DEVICE_ID "; " \
  /* DeviceName = nome exibido na interface web do Tasmota */ \
  "FriendlyName1 " DEVICE_ID "; " \
  /* FriendlyName1 = nome amigável principal */ \
  "MqttClient " UNIQUE_ID "; " \
  /* MqttClient = Client ID MQTT único; usa DEVICE_ID + final do MAC */ \
  "SetOption3 1; " \
  /* SetOption3 1 = habilita MQTT */ \
  "SetOption103 1; " \
  /* SetOption103 1 = ativa conexão MQTT com TLS */ \
  "Delay 2; " \
  \
  /* -------------------------------------------------- */ \
  /* COMPORTAMENTO DE ENERGIA / SEGURANÇA               */ \
  /* -------------------------------------------------- */ \
  "PowerOnState 3; " \
  /* PowerOnState 3 = ao voltar energia, retorna ao último estado */ \
  "SetOption65 1; " \
  /* SetOption65 1 = desativa reset de fábrica por ciclos rápidos de energia */ \
  "SetOption36 0; " \
  /* SetOption36 0 = desativa restauração automática de config em boot loop */ \
  \
  /* -------------------------------------------------- */ \
  /* MQTT / PUBLICAÇÃO                                  */ \
  /* -------------------------------------------------- */ \
  "SetOption4 1; " \
  /* SetOption4 1 = publica cada resposta MQTT em tópico separado */ \
  /* SetOption4 0 = usa resposta MQTT padrão em stat/RESULT */ \
  "SetOption89 1; " \
  /* SetOption89 1 = no Zigbee, publica mensagens em tópico individual por dispositivo */ \
  /* Exemplo: tele/DEVICE/0xABCD/SENSOR, dependendo do formato do FullTopic */ \
  "SetOption83 0; " \
  /* O script força SetOption83 0 para manter o JSON Zigbee usando o ShortAddr,*/ \
  /* como "0x97F6", em vez do nome amigável como chave principal. */ \
  "TelePeriod 60; " \
  /* TelePeriod 60 = envia telemetria a cada 60 segundos */ \
  \
  /* -------------------------------------------------- */ \
  /* GROUP TOPICS MQTT                                  */ \
  /* -------------------------------------------------- */ \
  "GroupTopic1 tasmotas; " \
  /* GroupTopic1 = grupo geral padrão dos dispositivos Tasmota */ \
  "GroupTopic2 Indiana; " \
  /* GroupTopic2 = grupo geral de todos os dispositivos Indiana */ \
  "GroupTopic3 " STORE_ID "; " \
  /* GroupTopic3 = grupo de todos os dispositivos da loja atual */ \
  "GroupTopic4 GW; " \
  /* GroupTopic4 = grupo de todos os gateways */ \
  "Delay 2; " \
  \
  /* -------------------------------------------------- */ \
  /* DATA / HORA                                        */ \
  /* -------------------------------------------------- */ \
  "Timezone -3; " \
  /* Timezone -3 = fuso horário UTC-3, Brasil/BRT */ \
  \
  /* -------------------------------------------------- */ \
  /* FINALIZAÇÃO                                        */ \
  /* -------------------------------------------------- */ \
  "Restart 1"
  /* Restart 1 = reinicia o Tasmota para aplicar toda a configuração */


#endif  // _USER_CONFIG_OVERRIDE_H_
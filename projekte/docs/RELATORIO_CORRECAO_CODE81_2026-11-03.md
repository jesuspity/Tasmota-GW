# Correção do erro Zigbee `Code 81` (ACK timeout) — Registro completo

**Data:** 03/11/2026 (00:15 – 02:00)
**Gateway de teste:** `PXXX_GW02_TEST` — IP `192.168.3.185` — ESP32-D0WD-V3, flash 4 MB, EFR32 (EZSP 6.7.10.0, protocolo 8)
**Resultado:** erro reproduzido com o firmware antigo e **eliminado** com o firmware novo nos testes reais (cabo do modem retirado).

---

## 1. Resumo para quem não vai ler tudo

| | Antes | Depois |
|---|---|---|
| Firmware | Tasmota 14.5.0 (build de terceiros ZB-GW03) | Tasmota 16.0.0 "Projekte+ GW v0.1" com **tarefa Zigbee prioritária** |
| Internet cai (Wi-Fi ok) | loop travou **10 s** → **Code 81** em 0,3 s → reinício | loop travou **22 s** e **38 s** → **sem Code 81**, Zigbee continuou |
| Memória livre (Heap) | 106 KB | ~50 KB (suficiente; ver item 8) |

**Causa:** quando a internet cai, o Tasmota tenta reconectar o MQTT/TLS e **trava todo o programa** enquanto espera (DNS + conexão + TLS). Nesse tempo ninguém responde à placa Zigbee. A EFR32 exige confirmação (ACK) de cada mensagem; sem resposta, desiste e envia o erro ASH `0x51` = `Code 81`. O próprio firmware então se reinicia sozinho.

**Correção:** uma tarefa separada, de prioridade maior que o loop principal, que lê a serial Zigbee a cada 5 ms e envia o ACK na hora. As mensagens ficam numa fila e o Tasmota as processa quando o MQTT libera o loop.

---

## 2. Linha do tempo da conversa

1. Leitura do dossiê, do `user_config_override.h`, do `.dmp`, do `Config Zigbee.rtf` e do código-fonte (Tasmota 16.0.0 pré-lançamento).
2. Leitura (somente consulta) do gateway: 14.5.0, Sleep 50, MqttRetry 10, KeepAlive 30, Timeout 4, DnsTimeout 1000, Rule2 desligada, ESP32 a **90 °C**.
3. Plano inicial: ajustes de tempo no `.h`. **Ajustado a pedido do usuário**: diminuir tempos já tinha sido testado sem efeito, então a correção precisava ser no firmware, dando prioridade ao Zigbee.
4. Backup da configuração: `Backups/Config_PXXX_GW02_TEST_antes_20261103_0058.dmp`.
5. Simulador de queda no Mac (`projekte/tools/simulador_queda.py`). Ele confirmou que cada tentativa de reconexão trava o gateway por ~5 s, mas **não reproduz o cenário real**: para isso o TLS teria que ser enfraquecido, o que foi recusado e está certo. O usuário observou que o erro acontece com a conexão estabelecida e a internet caindo depois.
6. **Teste real com o firmware antigo** (cabo do modem retirado): erro reproduzido (seção 4.1).
7. Implementação da tarefa Zigbee (seção 3), compilação e gravação.
8. Problemas encontrados e corrigidos (seção 6).
9. **Teste real com o firmware novo**, duas quedas: sem erro (seção 4.2).

---

## 3. Alterações no código

### 3.1 `tasmota/tasmota_xdrv_driver/xdrv_23_zigbee_9_serial.ino` (principal)
- Nova estrutura `ZbRx` com a tarefa FreeRTOS `ZbRx` (prioridade 5, mesmo núcleo do loop, leitura a cada 5 ms), a fila de 64 mensagens e um mutex recursivo.
- A leitura da serial EZSP foi movida para `ZigbeeEZSPReadFrame()`. A lógica é a mesma; agora ela pode rodar dentro da tarefa.
- `ZigbeeRxTaskAck()`: a tarefa envia o ACK ASH assim que a mensagem chega (só se couber na fila; se a fila estiver cheia, a EFR32 retransmite).
- `ZigbeeInputLoop()`: o loop principal pega **uma** mensagem por ciclo da fila, igual ao original (a máquina de estados precisa ver cada resposta antes da próxima).
- `ZigbeeEZSPSendRaw()`: protegido por mutex, para o ACK da tarefa nunca se misturar com um comando do loop.
- `ZigbeeProcessInputRaw()`: não repete o ACK quando a tarefa já o enviou.
- Diagnóstico no log: `ZIG: RX task started (priority 5, core 1)` e `ZIG: Main loop was busy, N frames acknowledged by RX task, max wait X ms`.
- Para voltar ao comportamento original: `#define ZIGBEE_EZSP_NO_RX_TASK`.

### 3.2 `tasmota/tasmota_xdrv_driver/xdrv_23_zigbee_A_impl.ino`
- A tarefa pausa enquanto o firmware da própria EFR32 está sendo atualizado (o atualizador XMODEM lê a serial diretamente).

### 3.3 `Config GW/user_config_override.h`
- Nova seção "ZIGBEE - TAREFA DE RECEPÇÃO PRIORITÁRIA", com `ZIGBEE_RX_TASK_PRIORITY 5`, `ZIGBEE_RX_TASK_PERIOD_MS 5` e a opção `ZIGBEE_EZSP_NO_RX_TASK` comentada. O restante do arquivo não mudou.

### 3.4 `Tasmota GW/platformio_override.ini` (novo)
- Ambiente `tasmota32-projekte-gw`: base enxuta `FIRMWARE_ZBBRDGPRO` (Zigbee Bridge Pro) mais o `user_config_override.h`, com a partição padrão de 2880 KB, igual à do gateway.

O diff completo está em `projekte/docs/patch_zigbee_rx_task.diff`.

---

## 4. Testes

Logs completos na pasta `Testes/`, gravados pelo Mac via rede local (funcionam sem internet).

### 4.1 Firmware antigo 14.5.0 — `log_teste2_cabo_firmware_atual.txt`
```
01:11:33.219 MQT: Ligando...                       <- internet caiu, tenta reconectar (loop trava)
01:11:43.226 MQT: TLS connection error: -1002      <- 10 s depois
01:11:43.493 ZbState Status 99 "ACK timeout" Code 81   <- 0,3 s depois: erro
-> gateway reiniciou (01:12)
```

### 4.2 Firmware novo 16.0.0 Projekte+ — `log_teste3_cabo_firmware_novo.txt`
```
1ª queda
01:52:06.277 ZIG: Main loop was busy, 3 frames acknowledged by RX task, max wait 22424 ms
             (sem Code 81; MQTT reconectou 01:53:55)
01:54 reinício manual pelo usuário -> "RX task started", Zigbee started
2ª queda
01:56:46.986 ZIG: Main loop was busy, 9 frames acknowledged by RX task, max wait 38106 ms
             (sem Code 81; cabo reconectado, MQTT voltou 01:57:30)
```
- Ocorrências de `Status 99` / `Code 81` no log: **0**.
- Após a volta da internet, os sensores continuaram publicando (TH01, TH02, TH05, TH06).
- TH03 (`0xE112`) e TH04 (`0xA944`) já estavam `Reachable:false`, sem comunicar havia ~13 dias **antes** dos testes. Provavelmente estão com a bateria fraca ou fora de alcance, e isso não tem relação com a correção.

---

## 5. Como compilar e gravar (procedimento validado)

**Compilar** (cópia local, fora do Google Drive, para não sincronizar GBs):
```bash
rsync -a --delete --exclude .git --exclude .pio "<Drive>/Firmware Tasmota/Tasmota GW/" ~/ProjekteBuild/TasmotaGW/
cp "<Drive>/Firmware Tasmota/Config GW/user_config_override.h" ~/ProjekteBuild/TasmotaGW/tasmota/
cd ~/ProjekteBuild/TasmotaGW && ~/.platformio/penv/bin/pio run -e tasmota32-projekte-gw
# resultado: build_output/firmware/tasmota32-projekte-gw.bin
```
> É necessário o PlatformIO **6.2.0** (instalado em `~/.platformio/penv`). O `pio` antigo do PATH (6.1.19) não compila este código.

**Gravar** (o gateway usa o esquema *safeboot*: o firmware só é gravado a partir da partição de segurança):
1. Console: `Restart 3` → reinicia no modo SAFEBOOT (13.2.0).
2. Na página web: **Atualizar Firmware** → enviar o `.bin` → reinicia sozinho no firmware novo.
3. **Restaurar configuração** com o `.dmp`, se necessário.

Pela linha de comando, é obrigatório abrir a página `/up` antes de enviar para `/u2`. Sem isso, o Tasmota responde "Not enough space".

Binário gravado: `Firmware Gerado/tasmota32-ProjektePlus-GW-ZbRxTask-16.0.0-enxuto.bin`
SHA-256 `edd6b48ef268f232ed18b5fa6a74d90ec6a342fc6176d1ac4aad1f04b5af441e`

---

## 6. Problemas encontrados no caminho

1. **Envio direto para `/u2` falhava** ("Não existe memória disponível" / "Not enough space"). A causa era o tipo de upload, que só é definido ao abrir a página `/up`. Resolvido.
2. **Variante `tasmota32` pesada demais:** 2,24 MB e só **32 KB** de memória livre. Trocada pela base Zigbee Bridge Pro: 1,56 MB e ~50 KB livres.
3. **Defeito na primeira versão da tarefa:** ela processava até 4 mensagens por ciclo. As respostas "rede criada" e "rede no ar" chegavam juntas, e a máquina de estados do Zigbee perdia a segunda (`timeout, goto label 99`, `Abort`). Corrigido para 1 mensagem por ciclo, como no original.
   - **Efeito colateral:** nesse período o Tasmota executou "Factory reset EZSP device" e recriou a rede na EFR32 **com os mesmos parâmetros** (canal 25, PanID 0x2501, ExtPanID e chaves do `Config Zigbee.rtf`). Os sensores reentraram na mesma rede.
4. O `pio` antigo do PATH não compila esta versão. Foi instalado o PlatformIO 6.2.0 no ambiente próprio do PlatformIO.

---

## 7. Configuração final verificada no gateway
- Firmware `16.0.0(Projekte+ GW v0.1)`, build 2026-11-03T01:43:30, Core 3.3.12 / IDF 5.5.5.
- `ZbConfig {"Channel":25,"PanID":"0x2501","ExtPanID":"0xCCCCCCCC00002501","KeyL":"<oculto>","KeyH":"<oculto>","TxRadio":20}`, igual ao `.rtf`.
- 6 dispositivos em `ZbStatus1`. Template "Gateway - Projekte+" mantido.
- MQTT `mqtt.projekteplus.com:8883`, TLS, usuário `<oculto>`, cliente `PXXX_GW02_TEST_6D9A30`.
- Rule2 continua **desligada**. Não é necessária: o firmware já reinicia sozinho se um Code 81 ainda acontecer.

---

## 8. Pontos de atenção e próximos passos

1. **Deixar rodando 24–72 h** neste gateway e repetir quedas de internet em horários diferentes antes de levar para a frota.
2. **Código-base pré-lançamento:** a pasta `Tasmota GW` é 16.0.0 *prerelease*. Para a frota, recomendo aplicar o `patch_zigbee_rx_task.diff` sobre uma versão estável quando ela sair.
3. **Outros gateways:** o `user_config_override.h` tem identidade `I267_GW01_FARM / Loja267`, que só é aplicada no primeiro boot ou em `Reset 1`. Ao gravar em outra unidade, mantenha a configuração dela ou restaure o `.dmp` da própria unidade.
4. **Memória:** ~50 KB livres (antes 106 KB). Funciona, inclusive com TLS. Se faltar memória, o primeiro candidato a sair é o **Berry** (usa ~17 KB).
5. **Temperatura:** o ESP32 mostrava 90 °C. O firmware novo roda a 160 MHz (antes 240 MHz), o que ajuda. Mesmo assim, vale verificar a ventilação da caixa.
6. **Sensores TH03 e TH04:** sem comunicação há ~13 dias. Verificar bateria e alcance.
7. **Página web travada durante a queda:** é esperado, porque o MQTT continua travando o loop principal. Só o Zigbee foi protegido. Os dados recebidos nesse tempo são processados quando o loop volta.
8. **Gravador de log** ainda rodando no Mac (`Testes/log_teste3_cabo_firmware_novo.txt`). Para parar: `pkill -f simulador_queda.py`.

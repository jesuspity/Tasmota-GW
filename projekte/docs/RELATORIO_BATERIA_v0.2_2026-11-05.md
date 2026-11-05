# Relatório da bateria de testes — 05/11/2026

- **Gateway:** 192.168.3.185 (`PXXX_GW02_TEST`)
- **Firmware:** `16.0.0(Projekte+ GW v0.2)`
- **Início:** 05/11 09:54:47  |  **Fim:** 05/11 11:54:24  |  **Duração:** 1h59m
- **Resultado geral:** **REPROVADO**

> **Resultado final corrigido: APROVADO (firmware v0.2).**
> - O "REPROVADO" abaixo vem só do bloco V, por 3 expectativas erradas do script de teste, e não de defeitos do firmware.
>   - O canal Zigbee 25 e o TelePeriod 600 são a configuração real deste gateway; o script esperava os valores padrão (20 e 60).
>   - O script leu errado a resposta do `ZbInfo`; os 4 sensores mandaram dados o tempo todo.
> - Com o script corrigido, o bloco V foi repetido às 11:57 e deu **26/26 APROVADO** (`verificacao_funcional_refeita/`).
> - **Não testado nesta bateria:** a falta de internet com a conexão TLS pendurada, porque troca o `MqttHost` e precisa de nova autorização. Esse caso foi validado na v0.1, em 03/11.
> - **Ponto de atenção:** temperatura do ESP32 sob estresse, com máxima de 115 °C e média de 103,6 °C (item C2).
> - A atualização OTA pelo novo link do GitHub foi testada antes da bateria: 1 min 40 s, configuração mantida.

## Resultado por bloco

| Bloco | Teste | Resultado | Duração | Code 81 | Quadros salvos | Falhas MQTT | Reconexões |
|---|---|---|---|---|---|---|---|
| A | Linha de base | APROVADO | 2min13s | 0 | 0 | 0 | 0 |
| V | Verificação funcional (23/26) | REPROVADO | 1min47s | 0 | 1 | 0 | 0 |
| D | Estresse web e comandos | APROVADO | 4min01s | 0 | 2 | 0 | 0 |
| E | Reinício (3× Restart 1) | APROVADO | 6min40s | 0 | 0 | 0 | 3 |
| S | Estresse pesado (web + rádio) | APROVADO | 1h44m | 0 | 3 | 0 | 0 |

**A – Linha de base**

- Página web: 30/30 respostas, média 416 ms, máx 710 ms

**V – Verificação funcional (23/26)**

- ✅ Versão do firmware é a v0.2: 16.0.0(Projekte+ GW v0.2)
- ✅ OtaUrl aponta para o GitHub público (C1): https://github.com/jesuspity/Tasmota-GW/releases/latest/download/tasmota32-projekte-gw.bin
- ✅ MQTT configurado (host e porta 8883): mqtt.projekteplus.com:8883
- ✅ Wi-Fi conectado: RSSI 100
- ✅ Hora sincronizada (NTP): 2026-11-05T09:57:06
- ✅ Memória livre > 30 KB: 51 KB, flash livre 2538 KB
- ✅ Zigbee: lista de dispositivos: 6 dispositivos
- ❌ Zigbee: configuração da rede (canal 20): canal 25, PanID 0x2501
- ❌ Sensores TH01/TH02/TH05/TH06 vistos nos últimos 30 min: 0/4
- ✅ Zigbee: ZbPermitJoin 0 respondido pelo rádio: 1
- ✅ Zigbee: ZbMap respondido pelo rádio: 1
- ✅ Regras (Rule2 legível): OFF
- ✅ Timers: ok
- ✅ Sistema de arquivos (UFS): 264 KB livres
- ✅ Berry: 2
- ❌ TelePeriod 60 s: 600
- ✅ Mensagem MQTT publicada: comando aceito
- ✅ Telemetria tele/STATE gerada no período: 1 mensagens em 75 s
- ✅ Página web /: 505 ms
- ✅ Página web /in: 1963 ms
- ✅ Página web /cn: 2156 ms
- ✅ Página web /md: 1859 ms
- ✅ Página web /cs: 2067 ms
- ✅ Página web /ufsd: 1918 ms
- ✅ Página web /bc: 2301 ms
- ✅ Página web /zbm: 1824 ms

**D – Estresse web e comandos**

- D1 web + comandos ao rádio: 915 ok / 213 falhas (mediana 1068 ms)
- D3 comandos: 100/100 respondidos (mediana 122 ms)

**E – Reinício (3× Restart 1)**

- E1.1: web 11s, Zigbee pronto 23s, MQTT 11s
- E1.2: web 13s, Zigbee pronto 22s, MQTT 11s
- E1.3: web 10s, Zigbee pronto 22s, MQTT 10s

**S – Estresse pesado (web + rádio)**

- Web sob estresse: 43571 ok / 1666 falhas
- Quedas reais de MQTT detectadas: 0

## Critérios

| Critério | Situação | Valor |
|---|---|---|
| Code 81 / Status 99 / ACK timeout | ✅ | 0 |
| ZbState Abort | ✅ | 0 |
| Reinício não comandado | ✅ | 0 |
| Exceção / watchdog | ✅ | 0 |
| Quadros perdidos na tarefa ZbRx | ✅ | 0 |
| Reconexão MQTT ≤ 60 s | ✅ | - |
| Heap mínimo > 30 KB | ✅ | 37–61 KB |
| Sem vazamento de memória (heap final ≥ inicial − 4 KB) | ✅ | 51.8 → 50.6 KB |
| Sensor TH01 sem silêncio > 30 min | ✅ | 91 leituras, maior silêncio 3min31s |
| Sensor TH02 sem silêncio > 30 min | ✅ | 122 leituras, maior silêncio 3min23s |
| Sensor TH05 sem silêncio > 30 min | ✅ | 110 leituras, maior silêncio 2min54s |
| Sensor TH06 sem silêncio > 30 min | ✅ | 165 leituras, maior silêncio 4min09s |

## Saúde do gateway

- Heap: mín 37 KB, máx 61 KB, média 50.7 KB
- Temperatura ESP32: mín 91.1 °C, máx 115.0 °C, média 103.6 °C
- LoadAvg: 19–985
- Leituras sem resposta HTTP (loop travado/reinício): 7 de 114
- Loop travado com quadros confirmados pela tarefa: 5 vezes, 6 quadros, espera máx 1726 ms
- ZbRouteError / MAC_INDIRECT_TIMEOUT: 2
- Reinícios: 3 detectados (0 não comandados)
- OtaUrl atual: `https://github.com/jesuspity/Tasmota-GW/releases/latest/download/tasmota32-projekte-gw.bin` (item C1 da revisão)
- Tarefa ZbRx: quadros perdidos 0, fora de sequência 0, pedidos nRdy 0, CRC ruim 0, time-out 0
- Telemetria tele/STATE vista no console: 14
- Perfil: 2h

## Quedas de internet simuladas

| Queda | Início | Duração | MQTT caiu | Reconexão após voltar |
|---|---|---|---|---|

## Quedas reais de MQTT detectadas (cabo / internet)

Nenhuma.

## Arquivos

- `bateria.log`: registro do script
- `metricas.csv`: leituras a cada 60 s
- log do simulador: console completo do gateway

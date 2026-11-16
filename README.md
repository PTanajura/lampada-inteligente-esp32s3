# 💡 Lâmpada Inteligente com ESP32-S3 e Arduino IoT Cloud

Firmware para uma lâmpada inteligente baseada na placa **ESP32-S3**. A lâmpada (LED RGB) acende automaticamente no escuro, muda de cor por um potenciômetro, é protegida por um sensor de temperatura com alarme sonoro e pode ser controlada remotamente por um painel online (Arduino IoT Cloud) ou pelo Monitor Serial.

---

## 📋 Índice

1. [Funcionalidades](#-funcionalidades)
2. [Lista de componentes](#-lista-de-componentes)
3. [Pinagem e ligações](#-pinagem-e-ligações)
4. [Pré-requisitos de software](#-pré-requisitos-de-software)
5. [Configuração do Arduino IoT Cloud](#-configuração-do-arduino-iot-cloud)
6. [Como gravar o firmware](#-como-gravar-o-firmware)
7. [Comandos disponíveis](#-comandos-disponíveis)
8. [Regras de funcionamento](#-regras-de-funcionamento)
9. [Saída do Monitor Serial](#-saída-do-monitor-serial)
10. [Calibração e ajustes](#-calibração-e-ajustes)
11. [Testes por componente](#-testes-por-componente)
12. [Solução de problemas](#-solução-de-problemas)
13. [Estrutura do projeto](#-estrutura-do-projeto)

---

## ✨ Funcionalidades

- **Monitor Serial:** mostra o estado do LED RGB (ON/OFF), a temperatura em ºC e o alerta **"Perigo! Desligar!"** quando a temperatura sai da faixa segura.
- **Painel online:** recebe comandos (ligar, desligar, cores, ativar/desativar sensores e buzzer) pelo Arduino IoT Cloud e publica temperatura, luminosidade e estado do LED.
- **Sensor de temperatura:** medição contínua e não bloqueante.
- **Fotorresistor (LDR):** liga o LED automaticamente quando o ambiente está escuro.
- **Buzzer:** soa enquanto a temperatura estiver abaixo de 0 ºC ou acima de 25 ºC.
- **Botão com interrupção externa:** liga/desliga LED e buzzer a cada pressão.
- **Potenciômetro:** define a cor do LED seguindo o espectro de temperatura de cor.
- **Funciona offline:** se o Wi-Fi cair, a lógica local (sensores, botão, LED, buzzer) continua operando.

---

## 🔧 Lista de componentes

| Qtd | Componente | Observação |
|---|---|---|
| 1 | Placa ESP32-S3 (DevKitC-1 ou similar) | |
| 1 | Sensor de temperatura **DS18B20** | Mede valores negativos |
| 1 | Resistor 4,7 kΩ | Pull-up do DS18B20 |
| 1 | Fotorresistor (LDR) | |
| 1 | Resistor 10 kΩ | Divisor de tensão do LDR |
| 1 | LED RGB | Cátodo comum (padrão) ou ânodo comum |
| 3 | Resistores ~220 Ω | Um por canal do LED RGB |
| 1 | Buzzer | Ativo ou passivo |
| 1 | Potenciômetro 10 kΩ | |
| 1 | Botão (push button) | |
| — | Protoboard e jumpers | |

---

## 🔌 Pinagem e ligações

| Componente | GPIO | Ligação |
|---|---|---|
| DHT11 | **12** | VCC → 3V3, GND → GND, resistor 4,7 kΩ entre DATA e 3V3 |
| LDR | **18** | LDR entre 3V3 e GPIO1; resistor 10 kΩ entre GPIO1 e GND |
| Potenciômetro (cursor) | **21** | Extremidades em 3V3 e GND |
| LED RGB – Vermelho | **41** | Resistor 220 Ω em série |
| LED RGB – Verde | **42** | Resistor 220 Ω em série |
| LED RGB – Azul | **40** | Resistor 220 Ω em série |
| LED RGB – comum | GND | Cátodo comum |
| Buzzer | **17** | Outro terminal no GND |
| Botão | **20** | Outro terminal no GND (pull-up interno ativado) |

```
3V3 ──┬── LDR ──┬── GPIO1
      │         └── 10kΩ ── GND
      │
      ├── 4,7kΩ ──┬── GPIO5 (DATA do DS18B20)
      │

3V3 ── Potenciômetro ── GND      (cursor → GPIO2)
GPIO15/16/17 ── 220Ω ── LED RGB (R/G/B) ── GND
GPIO18 ── Buzzer ── GND
GPIO7  ── Botão ── GND
```

> ⚠️ **Atenção aos pinos do ESP32-S3:** evite os GPIO 0, 3, 45 e 46 (strapping), 19 e 20 (USB) e 26–37 (flash/PSRAM). A pinagem acima já respeita essas restrições.
> Use sempre **3,3 V** nos sensores analógicos; os pinos do ESP32-S3 não toleram 5 V.

---

## 💻 Pré-requisitos de software

- [Arduino IDE 2.x](https://www.arduino.cc/en/software)
- Core **esp32 by Espressif Systems, versão 3.x** (Gerenciador de Placas)
- Placa selecionada: **ESP32S3 Dev Module**
- Bibliotecas (Gerenciador de Bibliotecas):
  - `ArduinoIoTCloud`
  - `Arduino_ConnectionHandler`
  - `OneWire`
  - `DallasTemperature`

> O código usa `ledcAttach()` e `ledcWrite()` por pino, que são a API do core **3.x**. No core 2.x esse código não compila.

---

## ☁️ Configuração do Arduino IoT Cloud

1. Crie uma conta em [cloud.arduino.cc](https://cloud.arduino.cc).
2. Em **Devices**, adicione um dispositivo **Third Party Device → ESP32 → ESP32S3 Dev Module**.
3. Guarde o **Device ID** e a **Secret Key** (ela só é exibida uma vez).
4. Em **Things**, crie um Thing, associe o dispositivo e adicione as variáveis:

| Nome | Tipo | Permissão | Atualização |
|---|---|---|---|
| `comando` | String | Read & Write | On change |
| `temperatura` | float | Read only | A cada 2 s |
| `luminosidade` | int | Read only | A cada 2 s |
| `ledLigado` | bool | Read only | On change |
| `alertaTemperatura` | bool | Read only | On change |

5. Em **Dashboards**, crie um painel com:
   - Um widget de **Messenger** (ou botões que enviem texto) ligado a `comando`;
   - Gauge/Chart para `temperatura` e `luminosidade`;
   - Indicadores (LED/Status) para `ledLigado` e `alertaTemperatura`.

---

## 🚀 Como gravar o firmware

1. Abra a pasta `lampada_inteligente` e o arquivo `lampada_inteligente.ino` na Arduino IDE.
   *(O nome da pasta deve ser igual ao nome do `.ino`.)*
2. Preencha as credenciais no início do arquivo:

   ```cpp
   const char WIFI_SSID[]         = "SEU_WIFI";
   const char WIFI_PASS[]         = "SUA_SENHA";
   const char DEVICE_LOGIN_NAME[] = "SEU_DEVICE_ID";
   const char SECRET_DEVICE_KEY[] = "SUA_SECRET_KEY";
   ```
3. Selecione a placa **ESP32S3 Dev Module** e a porta correta.
4. Se necessário, ative **USB CDC On Boot: Enabled** para ver o Monitor Serial pela porta USB nativa.
5. Clique em **Upload** e abra o Monitor Serial em **115200 baud**.

> 🔒 Não publique suas credenciais em repositórios públicos. Se for versionar o projeto, mova-as para um arquivo `arduino_secrets.h` e adicione-o ao `.gitignore`.

---

## 🎮 Comandos disponíveis

Enviados pela variável `comando` do painel **ou** digitados no Monitor Serial (com Enter). Não diferenciam maiúsculas de minúsculas.

| Comando | Efeito |
|---|---|
| `Ligar` | Força o LED RGB aceso |
| `Desligar` | Força o LED RGB apagado |
| `Vermelho` | LED vermelho por 1 s |
| `Amarelo` | LED amarelo por 1 s |
| `Azul` | LED azul por 1 s |
| `Desativar Temperatura` | Desativa o sensor de temperatura (e o alarme) |
| `Ativar Temperatura` | Reativa o sensor de temperatura |
| `Desativar Detector` | Desativa o fotorresistor |
| `Ativar Detector` | Reativa o fotorresistor e volta ao modo automático |
| `Desativar Buzzer` | Silencia o buzzer |
| `Ativar Buzzer` | Reativa o buzzer |

---

## 🧠 Regras de funcionamento

### LED RGB

| Condição | Resultado |
|---|---|
| Botão em OFF | LED apagado |
| Temperatura < 0 ºC ou > 25 ºC | LED apagado |
| Ambiente claro (modo automático) | LED apagado |
| Ambiente escuro e 0 ºC ≤ temp ≤ 25 ºC | LED aceso |
| Comando `Ligar` / `Desligar` | Força aceso/apagado (segurança e botão ainda têm prioridade) |

**Ordem de prioridade:** botão e segurança de temperatura → comandos `Ligar`/`Desligar` → detector de luz (automático).

### Cor pelo potenciômetro

A tensão do potenciômetro percorre o espectro de temperatura de cor:

```
Baixa            Média                          Alta
🔴 Vermelho → 🟠 Laranja → 🟡 Amarelo → ⚪ Branco → 🩵 Azul turquesa → 🔵 Azul
```

### Buzzer

Toca continuamente enquanto a temperatura estiver fora de 0–25 ºC, o buzzer estiver ativo e o sistema estiver ligado pelo botão.

### Botão

Usa **interrupção externa** (borda de descida) com debounce de 200 ms. Cada pressão alterna o sistema entre ON e OFF (LED e buzzer juntos).

---

## 🖥️ Saída do Monitor Serial

Atualizada a cada 1 segundo:

```
Sistema: ON  | LED RGB: ON | Temp: 22.4 C | Luz: 840 | Buzzer: ativo
Sistema: ON  | LED RGB: OFF | Temp: 27.1 C | Luz: 840 | Buzzer: ativo
Perigo! Desligar!
Sistema: OFF | LED RGB: OFF | Temp: 22.0 C | Luz: 1900 | Buzzer: ativo
```

---

## 🎛️ Calibração e ajustes

Todos os parâmetros ficam no início do `.ino`:

| Constante | Padrão | Descrição |
|---|---|---|
| `TEMP_MIN` / `TEMP_MAX` | 0 / 25 | Faixa segura de temperatura (ºC) |
| `LDR_LIMITE` | 1500 | Abaixo deste valor (ADC 0–4095) o ambiente é considerado escuro |
| `LED_CATODO_COMUM` | `true` | Use `false` para LED RGB de ânodo comum |
| `BUZZER_FREQ` | 2000 | Frequência do tom (Hz) |
| `TEMPO_COR_TEMP_MS` | 1000 | Duração dos comandos de cor |
| `DEBOUNCE_MS` | 200 | Debounce do botão |

**Calibrar o limite de luz:** observe o valor de "Luz" no Monitor Serial com o ambiente claro e depois com a mão cobrindo o LDR. Ajuste `LDR_LIMITE` para um valor entre os dois.

---

## 🧪 Testes por componente

Antes de integrar tudo, valide cada componente com pequenos sketches:

1. **LED RGB:** acenda R, G e B em sequência com `ledcWrite`.
2. **Potenciômetro:** imprima `analogRead(2)` e gire o botão (esperado: 0 a ~4095).
3. **LDR:** imprima `analogRead(1)` com e sem luz.
4. **DS18B20:** imprima `sensorTemp.getTempCByIndex(0)`; aqueça o sensor com a mão para ver a variação.
5. **Buzzer:** use `tone(18, 2000)` por 1 s.
6. **Botão:** conte as interrupções em uma variável `volatile` e imprima o valor.

---

## 🛠️ Solução de problemas

| Problema | Possível causa / solução |
|---|---|
| `ledcAttach` não encontrado | Core esp32 está na versão 2.x. Atualize para a 3.x |
| Sem saída no Monitor Serial | Ative *USB CDC On Boot* ou use a porta UART da placa |
| `[ERRO] DS18B20 nao encontrado` | Verifique o pull-up de 4,7 kΩ, o pino DATA e a alimentação |
| Temperatura `-127` | Sensor desconectado ou sem pull-up |
| LED com cores trocadas ou invertido | Confira a ordem R/G/B e ajuste `LED_CATODO_COMUM` |
| LED sempre apagado | Ambiente claro, temperatura fora da faixa, ou sistema desligado pelo botão |
| Botão alterna sozinho | Verifique a ligação ao GND; aumente `DEBOUNCE_MS` |
| Não conecta ao Cloud | Revise SSID/senha (Wi-Fi 2,4 GHz), Device ID e Secret Key |
| Comando repetido não funciona | O código limpa `comando` após executar; confirme que o painel envia o texto completo |

---

## 📁 Estrutura do projeto

```
lampada_inteligente/
├── lampada_inteligente.ino   # Firmware principal
├── banco.py                  # Arquivo para o banco
└── README.md                 # Este arquivo
```

---

## 📄 Licença

Projeto desenvolvido para fins educacionais em laboratório. Use e modifique livremente.

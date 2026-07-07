# ReDuino-Counter — Sistema de Monitoramento de Fluxo RF

Este projeto consiste em um sistema de contagem e monitoramento de fluxo de pessoas em tempo real. O sistema utiliza sensores ultrassônicos acoplados a transceptores de rádio NRF24L01, comunicando-se com um receptor central através de um protocolo baseado no padrão MACAW (handshake RTS ➜ CTS ➜ DATA ➜ ACK). O receptor central encaminha os dados via interface Serial (JSON) para um backend em Python, que disponibiliza as informações para um painel web.

---

## Painel de Visualização

Abaixo consta a imagem ilustrativa da interface gráfica do painel:

![Painel de Fluxo](painel.png)

---

## Arquitetura do Sistema

A arquitetura do projeto é estruturada em três partes:
1. **Firmware (Arduino):** Responsável por ler os sensores ultrassônicos e realizar a transmissão sem fio via rádio.
2. **Backend (Python):** Efetua a leitura dos dados recebidos pela porta serial, armazena o histórico em memória e expõe endpoints de API REST.
3. **Frontend (Web):** Consome a API REST e renderiza as informações no painel de monitoramento.

---

## Estrutura do Firmware (Placas Arduino)

A comunicação de rádio é realizada no canal `12` com taxa de transmissão configurada em `250kbps`. A estrutura de pacotes compartilhada entre as placas é definida como:

```cpp
struct Pacote {
  uint8_t tipo;       // 1=RTS, 2=CTS, 3=DADOS, 4=ACK
  uint8_t id;         // Identificador do nó transmissor
  float distancia;    // Distância em centímetros medida pelo sensor
};
```

### Placas Sensoras

*   **Sensor de Entrada ([entrada_47.ino](entrada_47/entrada_47.ino)):**
    *   **Hardware:** Sensor ultrassônico HC-SR04 e rádio NRF24L01.
    *   **Pinagem:** Sensor conectado aos pinos A2 (Trig) e A3 (Echo); Rádio nos pinos D7 (CE) e D8 (CSN).
    *   **Identificador:** ID físico `47`.
    *   **Protocolo:** Transmite uma solicitação RTS, aguarda o sinal de autorização CTS do receptor, envia os dados de distância e espera pela confirmação ACK. Se ocorrer falha no handshake, realiza recuo aleatório (backoff) antes de nova tentativa.
*   **Sensor de Saída ([saida_15.ino](saida_15/saida_15.ino)):**
    *   **Hardware e Pinagem:** Idênticos ao sensor de entrada.
    *   **Identificador:** ID físico `15`.

### Receptor Central / Gateway ([receptor_5.ino](receptor_5/receptor_5.ino))

*   **Hardware:** Rádio NRF24L01 conectado à placa Arduino, conectada via USB Serial ao computador.
*   **Identificação de Dispositivos:** No código do receptor, o identificador de rádio `15` é associado à entrada (`ID_ENTRADA`) e o identificador `47` é associado à saída (`ID_SAIDA`). Esse mapeamento é o que define o papel funcional de cada placa — os sufixos `_47`/`_15` nos nomes dos arquivos das placas sensoras (e suas constantes locais `ID_ENTRADA`/`ID_SAIDA`) indicam apenas o ID de rádio de cada placa, não o papel que ela exerce no sistema.
*   **Funcionamento:** Responsável por responder às solicitações de rádio das placas sensoras, verificar se a distância está abaixo do limite de `LIMIAR_CM` (15 cm) e aplicar lógica de debounce de estado para registrar cada passagem de pessoa uma única vez. Envia as informações em formato JSON pela porta Serial e escuta o comando de reset para reiniciar os contadores.

---

## Estrutura do Software

### Backend ([app.py](app.py))

Desenvolvido em Python com Flask, o backend executa em segundo plano a escuta contínua de dados da porta serial configurada. Caso o parâmetro `SERIAL_PORT` esteja definido como `auto` no arquivo `.env`, o backend testa as portas disponíveis buscando mensagens válidas em JSON.

**API REST:**
*   `GET /api/status`: Retorna o estado atual do sistema (ocupação atual, totais acumulados, distâncias dos sensores e erros de pacotes).
*   `GET /api/historico`: Retorna a lista contendo o histórico recente de leituras completas.
*   `GET /api/eventos`: Retorna o log com registros individuais de entradas e saídas.
*   `POST /api/reset`: Limpa o histórico de dados e envia o sinal via serial para redefinir as variáveis do receptor.
*   `GET /api/health`: Retorna a integridade do serviço e da conexão serial.

### Frontend ([index.html](index.html))

Uma página estática em HTML, CSS e JavaScript que atualiza a interface a cada 1,2 segundos através de chamadas à API REST:
*   Exibe contadores de entradas, saídas e a ocupação líquida do ambiente.
*   Renderiza barras dinâmicas que indicam a distância nos sensores.
*   Apresenta um gráfico simples de linha (SVG Sparkline) do histórico de ocupação.
*   Exibe uma seção com o monitoramento do protocolo de rádio indicando o fluxo do handshake (RTS/CTS/DATA/ACK) ao vivo.

---

## Instruções para Execução

1.  **Gravação das Placas:** Carregue os respectivos firmwares nas placas sensoras e na placa receptora.
2.  **Preparação do Backend:** Instale as dependências listadas:
    ```bash
    pip install -r requirements.txt
    ```
3.  **Configurações:** Copie o arquivo `.env.example` para `.env` e configure a porta de conexão serial em `SERIAL_PORT`.
4.  **Inicialização:** Execute o backend com:
    ```bash
    python app.py
    ```
5.  **Navegação:** Acesse `http://localhost:3001` no navegador.

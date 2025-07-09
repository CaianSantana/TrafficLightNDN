# Guia de Cenários de Simulação

Este documento detalha a estrutura dos arquivos de cenário (`.yaml`) utilizados pelo projeto, explica os cenários pré-configurados e orienta sobre como criar e aplicar suas próprias configurações de tráfego.

## 1. Estrutura do Arquivo de Cenário (`.yaml`)

Cada cenário é definido por um arquivo YAML contendo quatro seções principais: `traffic-lights`, `intersections`, `green_waves`, e `sync_groups`.

### 1.1 `traffic-lights`
Esta é a seção principal e obrigatória. Ela define uma lista de todos os semáforos que existem na simulação.

-   **`name`**: O nome único do semáforo no formato de um prefixo NDN. É usado para identificar e endereçar o semáforo na rede.
-   **`cycle_time`**: O tempo total do ciclo do semáforo em segundos (soma de um período de verde + amarelo + vermelho).
-   **`state`**: O estado inicial do semáforo ao iniciar a simulação (`GREEN` ou `RED`).
-   **`columns`**: Número de colunas de veículos que a via suporta. Usado para calcular a prioridade.
-   **`lines`**: Número de faixas (linhas) de veículos. Usado para calcular a prioridade.
-   **`intensity`**: Um indicador de intensidade de fluxo (`LOW`, `MEDIUM`, `HIGH`). Usado para calcular a prioridade.

### 1.2 `intersections`
Define uma lista de cruzamentos, onde o estado de um semáforo deve ser oposto ao outro.

-   **`name`**: Nome descritivo para o cruzamento.
-   **`traffic-lights`**: Uma lista com os nomes (prefixos NDN) dos semáforos que compõem o cruzamento.

### 1.3 `green_waves`
Define uma lista de "ondas verdes", que são sequências de semáforos que abrem em sucessão para criar um fluxo contínuo de tráfego.

-   **`name`**: Nome descritivo para a onda verde.
-   **`traffic_lights`**: Uma lista ordenada de nomes de semáforos. A ordem é crucial, pois define a sequência em que os semáforos devem abrir. O primeiro da lista é o "líder" da onda.
-   **`travel_time_ms`**: O tempo médio de deslocamento, em milissegundos, entre um semáforo e o próximo na sequência.

### 1.4 `sync_groups`
Define uma lista de grupos de semáforos que, por alguma razão, precisam operar com o mesmo estado e tempo (ex: duas travessias de pedestres próximas).

-   **`name`**: Nome descritivo para o grupo de sincronia.
-   **`traffic_lights`**: Uma lista de nomes de semáforos que devem ser sincronizados.

---

## 2. Cenários Existentes

Abaixo estão os detalhes dos cenários fornecidos.

### 2.1 Cenário 1: Cabula (`cabula.yaml`)

Este cenário simula o tráfego em uma área complexa e movimentada de Salvador, envolvendo a Ladeira do Cabula e a saída de Pernambuês.

-   **Semáforos (5 no total):**
    -   Dois na Rua dos Rodoviários (`/ssa/r-rodoviarios/s-cabula/...`).
    -   Um na Av. Silveira Martins, sentido Cabula (`/ssa/r-silveira-martins/s-cabula/1`).
    -   Um na Av. Silveira Martins, sentido Rótula do Abacaxi (`/ssa/r-silveira-martins/s-rotula-abacaxi/1`).
    -   Um na Rua Thomaz Gonzaga, sentido Rótula (`/ssa/r-thomaz-gonzaga/s-rotula-abacaxi/1`).

-   **Cruzamento (`intersection`):**
    -   `praca-f-manoel`: Simula um cruzamento entre um semáforo da Rua dos Rodoviários e um da Rua Thomaz Gonzaga.

-   **Onda Verde (`green_wave`):**
    -   `s-cabula-1`: Cria um fluxo contínuo ao longo da Rua dos Rodoviários em direção à Av. Silveira Martins.

-   **Grupo de Sincronia (`sync_group`):**
    -   `travessia-pedestres-s-cabula-1`: Sincroniza dois semáforos cooperantes na Av. Silveira Martins que estão na mesma via em sentidos diferentes. Esses semáforos servem para permitir a travessia de pedestres.

### 2.2 Cenário 2: Dois Leões (`dois-leoes.yaml`)

Este cenário modela o tráfego na região do Largo Dois Leões e Sete Portas, outra área de grande fluxo em Salvador.

-   **Semáforos (5 no total):**
    -   Um no Largo Dois Leões, sentido Baixa de Quintas.
    -   Dois na Rua Cônego Pereira, na altura de Sete Portas.
    -   Um na Av. Glauber Rocha.
    -   Um no Largo Dois Leões, sentido Sete Portas.

-   **Cruzamento (`intersection`):**
    -   `dois-leoesxconego-pereira`: Modela a interseção principal entre o Largo Dois Leões e a Rua Cônego Pereira.

-   **Onda Verde (`green_wave`):**
    -   `conego-pereira`: Garante um fluxo de tráfego coordenado ao longo da Rua Cônego Pereira, com um tempo de deslocamento de 3 segundos entre os semáforos.

-   **Grupos de Sincronia (`sync_groups`):**
    -   `conego-pereiraxglauber-rocha`: Sincroniza um semáforo da Cônego Pereira com um da Av. Glauber Rocha.
    -   `conego-pereiraxdois-leoes`: Sincroniza outro semáforo da Cônego Pereira com um do Largo Dois Leões.

---

## 3. Criando seu Próprio Cenário

Siga estes passos para criar um novo cenário de simulação:

1.  **Crie o Arquivo:** Dentro da pasta `scenarios/`, crie um novo arquivo com a extensão `.yaml` (ex: `meu_cenario.yaml`).

2.  **Defina os Semáforos:** Comece adicionando a seção `traffic-lights` e liste todos os semáforos que farão parte da sua simulação. Atribua nomes NDN únicos e preencha os parâmetros `cycle_time`, `state`, etc.

3.  **Configure os Grupos (Opcional):** Se necessário, adicione as seções `intersections`, `green_waves`, e/ou `sync_groups`. Dentro delas, referencie os semáforos usando os mesmos nomes NDN definidos no passo anterior.

4.  **Salve o Arquivo:** Seu novo cenário está pronto para ser usado.

---

## 4. Aplicando um Novo Cenário no Docker Compose

O arquivo `docker-compose.yml` está configurado para o cenário padrão com 5 semáforos. Para usar um cenário diferente, você precisa editá-lo.

### Passo 1: Atualizar o Nome do Arquivo do Cenário

Você precisa informar ao `orchestrator` e aos `trafficlight` qual arquivo de cenário carregar.

-   Abra o arquivo `.env`.
-   Troque o nome do arquivo `.yaml` pelo seu.

### Passo 2: Ajustar o Número de Semáforos

O `docker-compose.yml` define um serviço para cada agente semáforo (`trafficlight_0`, `trafficlight_1`, etc.). Você deve ajustar o número de serviços para corresponder ao número de semáforos no seu novo cenário.

-   **Se o seu cenário tem MENOS semáforos (ex: 3):**
    Simplesmente apague os serviços excedentes do arquivo `docker-compose.yml`. No exemplo, você removeria `trafficlight_3` e `trafficlight_4`.

-   **Se o seu cenário tem MAIS semáforos (ex: 6):**
    Copie um bloco de serviço `trafficlight` existente, cole-o no final da lista de serviços e atualize seu nome e o ID do semáforo.

**Exemplo:** Adicionando um 6º semáforo (`trafficlight-5`):

```yaml
# ... outros serviços ...

  trafficlight-4:
    # ... (configuração do semáforo 4) ...
    environment:
      - ID_SEMAFORO=4

  # Bloco novo adicionado para o 6º semáforo
  trafficlight-5:
    build: .
    container_name: trafficlight-5
    networks:
      - ndn_network
    cap_add:
      - NET_ADMIN
    volumes:
      - ./scenarios:/app/scenarios:ro
      - ./config:/app/config:ro
    environment:
      - ROLE=trafficlight
    command: ["trafficLight", "/app/${SCENARIO_FILE}", "5", "INFO"]
```
**Atenção:** O o terceiro argumento do `command` deve corresponder ao índice do semáforo na lista do seu arquivo `.yaml` (começando em 0).

### Passo 3: Reiniciar os Serviços

Após salvar suas alterações no `docker-compose.yml`, execute o seguinte comando para aplicar a nova configuração:

```bash
docker-compose up --build -d
```
O comando irá reconstruir a imagem se necessário e iniciar os contêineres com a nova topologia.
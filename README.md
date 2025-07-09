# Orquestrador de Semáforos Inteligentes com NDN

Este projeto implementa um sistema de orquestração para semáforos inteligentes utilizando a arquitetura de Redes Orientadas a Dados (Named Data Networking - NDN). O sistema permite a coordenação de múltiplos semáforos para criar rotas sincronizadas (ondas verdes) e grupos de semáforos que operam em conjunto, tudo configurado através de arquivos de cenário em formato YAML.

O projeto consiste em duas aplicações principais:
1.  **Orchestrator**: A entidade central que lê um cenário, monitora o estado de todos os semáforos e envia comandos de coordenação.
2.  **TrafficLight**: A aplicação que simula um semáforo, reportando seu estado e recebendo comandos do orquestrador.

---

## Estrutura do Projeto

Aqui está uma visão geral dos diretórios e arquivos importantes do projeto:

-   `build/`: Diretório de destino para os arquivos de compilação gerados pelo CMake. 
-   `config/`: Armazena arquivos de configuração.
    -   `config/trust-schema.conf`: Define as regras de confiança para validação de certificados e pacotes de dados no NDN.
-   `include/`: Arquivos de cabeçalho (`.hpp`) com as definições de classes e estruturas.
    -   `include/Orchestrator.hpp`: Definição da classe do orquestrador central.
    -   `include/SmartTrafficLight.hpp`: Definição da classe que representa o semáforo.
    -   `include/YamlParser.hpp`: Definição do parser de arquivos de cenário YAML.
    -   `include/Structs.hpp`, `Enums.hpp`, `LogLevel.hpp`: Definições de estruturas de dados, enums e níveis de log usados no projeto.
-   `main/`: Contém os pontos de entrada (`main`) das aplicações.
    -   `main/mainOrchestrator.cpp`: Ponto de entrada para o executável `orchestrator`.
    -   `main/mainSTL.cpp`: Ponto de entrada para o executável `trafficLight`.
-   `metrics/`: Armazena métricas coletadas durante a execução.
-   `scenarios/`: Contém os arquivos de cenário (`.yaml`) que definem as topologias de semáforos.
    -   **Consulte o `scenarios/README.md` para um guia detalhado sobre os cenários existentes e como criar o seu!**
-   `src/`: Arquivos de implementação (`.cpp`) das classes definidas em `include/`.
-   `Dockerfile`: Define a imagem Docker para a aplicação, contendo todas as dependências e o código compilado.
-   `docker-compose.yml`: Orquestra a execução dos contêineres, incluindo o NFD, o orquestrador e os semáforos.
-   `install-NFD.sh`: Script para instalação manual do NFD e `ndn-cxx` no diretório home do usuário.
-   `entrypoint.sh`: Script executado ao iniciar o contêiner Docker para configurar o ambiente e iniciar os processos.

---

## Formas de Deploy

Existem duas maneiras de configurar e executar este projeto: via Docker Compose (recomendado para simplicidade) ou manualmente.

### Opção 1: Deploy com Docker Compose (Recomendado)

Este método automatiza a instalação de dependências, configuração de rede e execução de todos os componentes.

#### Pré-requisitos
-   [Docker](https://docs.docker.com/get-docker/)
-   [Docker Compose](https://docs.docker.com/compose/install/)

#### Passo a Passo
1.  **Clone o repositório:**
    ```bash
    git clone https://github.com/CaianSantana/TrafficLightNDN.git
    cd TrafficLightNDN/
    ```

2.  **Ajuste o cenário (opcional):**
    O arquivo `docker-compose.yml` está configurado para usar o cenário `scenarios/dois-leoes.yaml` por padrão. Se desejar usar outro, edite a seção `SCENARIO_FILE` do arquivo `.env`. Mais detalhes em `scenarios/README.md`

3.  **Inicie os serviços:**
    ```bash
    docker-compose up --build -d
    ```
    Este comando irá:
    -   Construir a imagem Docker definida no `Dockerfile`, instalando todas as dependências do NDN.
    -   Iniciar um contêiner para o NFD (Named Data Networking Forwarding Daemon).
    -   Iniciar um contêiner para o `orchestrator`.
    -   Iniciar múltiplos contêineres `trafficlight`, conforme definido no `docker-compose.yml`.

4. **Para monitorar os logs:**
   Abra múltiplos terminais. No primeiro, execute o orquestrador:
   ```bash
    docker-compose logs -f orchestrator
   ```
    Nos demais:
    ```bash
    docker-compose logs -f trafficlight-<id>
    ```

1.  **Para parar os serviços:**
    Execute:
    ```bash
    docker-compose down
    ```

---

### Opção 2: Deploy Manual

Este método é ideal para desenvolvimento e depuração, pois oferece controle total sobre cada componente.

#### Pré-requisitos
-   Um compilador C++ (g++, clang++)
-   CMake (versão 3.16+)
-   pkg-config
-   libssl-dev e libcrypto-dev

#### Passo 1: Instalar NFD e ndn-cxx
O script `install-NFD.sh` automatiza a instalação do NFD e da biblioteca `ndn-cxx` no diretório `~/nfd` do seu usuário, sem necessidade de permissões `sudo`.

```bash
# Conceda permissão de execução e rode o script
chmod +x install-NFD.sh
./install-NFD.sh
```

#### Passo 2: Iniciar o NFD
Com as dependências instaladas, inicie o daemon de encaminhamento:
```bash
sudo nfd start
```

#### Passo 3: Configurar Identidades e Confiança
Para que as aplicações possam assinar e validar pacotes, precisamos criar as identidades NDN.

1.  **Gerar identidade da "trust anchor" (autoridade certificadora raiz):**
    ```bash
    ndnsec key-gen /app
    ```

2.  **Gerar identidade para a aplicação:**
    ```bash
    ndnsec key-gen /app
    ```

3.  **Assinar a chave da aplicação com a trust anchor:**
    ```bash
    ndnsec cert-install -s /app
    ```

4.  **Configurar o `trust-schema.conf`:**
    Certifique-se de que o arquivo `config/trust-schema.conf` existe e aponta para a sua trust anchor. Exemplo:
    ```ini
    ; config/trust-schema.conf
    trust_anchor {
        type: "ndn-key"
        key_name: "/app"
    }
    ```
    *Nota: a sintaxe do schema pode variar. Consulte a documentação do `ndn-cxx` para a versão que você está usando.*

#### Passo 4: Compilar o Projeto
Use o CMake para compilar os executáveis.

```bash
cd build/
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
cmake ..
make
mv orchestrator trafficLight ..
```

#### Passo 5: Executar as Aplicações
Abra terminais separados para cada componente.

1.  **Terminal 1: Orquestrador**
    ```bash
    ./build/orchestrator scenarios/cabula.yaml INFO
    ```
    *Sintaxe: `./build/orchestrator <caminho_yaml> <log_level>`*

2.  **Terminal 2: Semáforo 1**
    ```bash
    ./build/trafficLight scenarios/cabula.yaml 0 INFO
    ```
    *Sintaxe: `./build/trafficLight <caminho_yaml> <id_semaforo> <log_level>`*

3.  **Terminal 3: Semáforo 2**
    ```bash
    ./build/trafficLight scenarios/cabula.yaml 1 INFO
    ```
    ...e assim por diante para cada semáforo definido no seu arquivo YAML.

---

## Considerações Finais
-   Garanta sempre que o **NFD está rodando** (`nfd-status`) antes de executar as aplicações.
-   Use o comando `ndnsec-ls` (ou `ndnsec key-list`) para listar suas identidades e verificar se estão corretas.
-   Os logs do NFD (geralmente em `/var/log/nfd` ou via `journalctl`) são úteis para diagnosticar problemas de rota ou validação.
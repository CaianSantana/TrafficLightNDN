# Guia para Configuração e Execução do Exemplo Producer/Consumer NDN

Este documento descreve o passo a passo para configurar o ambiente NDN, criar identidades, configurar o trust schema e compilar/rodar o exemplo básico de produtor e consumidor usando a biblioteca `ndn-cxx`.

---

## 1. NFD (Named Data Networking Forwarding Daemon)

O NFD é o daemon responsável pelo encaminhamento de pacotes na rede NDN local.

### 1.1 Instalação

Para instalar o NFD em distribuições baseadas em Debian/Ubuntu, você pode usar os comandos abaixo (ou consulte o site oficial para outras distros):

```bash
sudo apt update
sudo apt install nfd
```

Ou, para sistemas que não tenham pacote pronto, compile a partir do [repositório oficial](https://github.com/named-data/NFD).

### Por que instalar o NFD?

O NFD é o componente que realiza o roteamento dos Interest e Data na rede NDN, permitindo que aplicações produtoras e consumidoras se comuniquem.

---

### 1.2 Execução

Após instalar, execute o daemon NFD com:

```bash
nfd-start
```

Para verificar o status e configuração do NFD:

```bash
nfd-status
```

Para parar o serviço:

```bash
nfd-stop
```

---

## 2. Criação do arquivo `trust-schema.conf`

O arquivo `config/trust-schema.conf` é usado para configurar as regras de confiança para a validação dos certificados e Data packets.

### Por que criar esse arquivo?

Sem uma configuração adequada do esquema de confiança, o NFD e as bibliotecas de segurança do NDN não conseguem validar as assinaturas, resultando em erros durante a execução dos produtores e consumidores.

### Exemplo básico de `config/trust-schema.conf`:

Crie o arquivo `config/trust-schema.conf` com o seguinte conteúdo:

```
<trust-anchor>
  <prefix>/example</prefix>
</trust-anchor>

<key-locator>
  <name>/example/KEY/123456789</name>
</key-locator>
```

Salve esse arquivo na mesma pasta do seu executável ou no caminho padrão esperado pelo NFD.

---

## 3. Gerando a identidade padrão

Para o produtor assinar os Data packets, precisamos gerar uma identidade e um certificado válido.

### Por que gerar uma identidade?

A assinatura dos dados é fundamental para garantir autenticidade e integridade no NDN. O KeyChain precisa de uma identidade configurada para assinar os pacotes de dados.

### Passo a passo para gerar identidades e certificados:

Execute os seguintes comandos:

```bash
# Gerar uma identidade "exemplo" que será a trust anchor
ndnsec key-gen /example

# Exportar o certificado da trust anchor
ndnsec cert-dump -i /example > example-trust-anchor.cert

# Gerar a identidade do produtor
ndnsec key-gen /example/testApp

# Criar e instalar o certificado do produtor, assinado pela trust anchor
ndnsec sign-req /example/testApp | ndnsec cert-gen -s /example -i example | ndnsec cert-install -
```

### Explicação:

- `ndnsec key-gen /example` cria uma chave e identidade para o trust anchor.
- `ndnsec cert-dump` exporta o certificado para uso.
- `ndnsec key-gen /example/testApp` cria uma identidade para o produtor.
- `ndnsec sign-req` gera um pedido de assinatura para a identidade do produtor.
- `ndnsec cert-gen` gera o certificado assinado pela trust anchor.
- `ndnsec cert-install` instala o certificado na chave do produtor.

---

## 4. Compilação e execução

### 4.1 Compilação

```bash
cd build/
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
cmake ..
make
mv orchestrator trafficLight ..
```


### 4.2 Execução do Orchestrator

Execute o Orquestrador:

```bash
cd ..
./orchestrator <caminho_yaml> <log_level>
```
Níveis de log disponíveis: NONE, ERROR, INFO, DEBUG

---
### 4.3 Execução do(s) Semáforo(s)

Execute o Orquestrador:

```bash
cd ..
./trafficLight <caminho_yaml> <id_semaforo> <log_level>

```
Níveis de log disponíveis: NONE, ERROR, INFO, DEBUG

---

## Considerações Finais

- Garanta que o NFD está rodando antes de executar producer e consumer.
- Verifique se o `trust-schema.conf` está no local correto e acessível.
- Sempre que gerar novas identidades e certificados, confirme que estão instalados corretamente no KeyChain.
- Use o comando `ndnsec key-list` para listar suas identidades e chaves.
- Logs do NFD podem ajudar a diagnosticar problemas de rota e confiança.

---

## Referências

- [NDN Developer Guide](https://named-data.net/doc/NDN-cxx/current/)
- [NFD Official GitHub](https://github.com/named-data/NFD)
- [ndnsec CLI tool documentation](https://named-data.net/doc/ndnsec/current/)

---


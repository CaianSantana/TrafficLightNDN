#!/bin/bash
set -e

DEFAULT_KEY_PATH="/root/.ndn/keys/localhost.key"
if [ ! -f "$DEFAULT_KEY_PATH" ]; then
  echo "Nenhuma identidade padrão encontrada. Criando uma nova..."
  ndnsec key-gen /app                       
  ndnsec cert-dump -i /app > app-trust-anchor.cert
  ndnsec  key-gen /app      
  ndnsec sign-req /app | ndnsec cert-gen -s /app -i app | ndnsec cert-install -
else
  echo "Identidade padrão encontrada."
fi

if [ -z "$ROLE" ]; then
  echo "ERRO: A variável de ambiente ROLE deve ser definida ('orchestrator' ou 'trafficlight')."
  exit 1
fi

echo "[$HOSTNAME] Iniciando com ROLE=$ROLE..."

nfd &
echo "[$HOSTNAME] NFD iniciado em background. Aguardando 3 segundos para estabilizar..."
sleep 3


if [ "$ROLE" == "orchestrator" ]; then
  ORCH_NDN_NAME="/central"
  CONFIG_FILE=$2
  
  echo "[$HOSTNAME] Lendo a configuração de semáforos de $CONFIG_FILE..."

  yq -o=json . "$CONFIG_FILE" | jq -r '.["traffic-lights"] | to_entries[] | "trafficlight-\(.key) \(.value.name)"' | while read -r TL_CONTAINER TL_NDN_NAME; do
    echo "[$HOSTNAME] Configurando rota para: $TL_CONTAINER"

    FACE_ID=$(nfdc face create "udp://$TL_CONTAINER" | awk -F'id=' '{print $2}' | awk '{print $1}')
    nfdc route add "$TL_NDN_NAME" nexthop "$FACE_ID"
    echo "[$HOSTNAME]   Rota para $TL_NDN_NAME via FaceID $FACE_ID criada."
  done
  echo "[$HOSTNAME] Configuração de rotas para todos os semáforos finalizada."

elif [ "$ROLE" == "trafficlight" ]; then
  ORCH_CONTAINER="orchestrator"
  ORCH_NDN_NAME="/central"

  echo "[$HOSTNAME] Configurando rota para o orquestrador..."
  
  FACE_ID=$(nfdc face create "udp://$ORCH_CONTAINER" | awk -F'id=' '{print $2}' | awk '{print $1}')
  nfdc route add "$ORCH_NDN_NAME" nexthop "$FACE_ID"
  echo "[$HOSTNAME]   Rota para $ORCH_NDN_NAME via FaceID $FACE_ID criada."
fi

echo "[$HOSTNAME] Configuração de rede finalizada."
echo "[$HOSTNAME] Iniciando aplicação principal: $@"

exec "$@"
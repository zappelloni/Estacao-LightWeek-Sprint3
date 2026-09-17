/*
  ============================================================
  ESTACAO LIGHTWEEK - SPRINT 3 (Prototipagem Funcional e Integracao)
  ============================================================
  Disciplina : SERS - André Tritiack
  Turma      : 1CCPK
  Equipe     : Maykon de Lima Silva      - RM 574022
               Felipe Pereira Restivo    - RM 570712
               Gabriel Rodrigues Zappelloni - RM 572060
               Rodger Costa Rios         - RM 571438
               Kenichi Caio Yamamoto     - RM 569815

  Este código evolui o protótipo da Sprint 2 (LEDs indicando
  vagas ocupadas + leitura de placa solar) adicionando:

    1) Sessões de recarga estruturadas (por vaga), com horário
       de início/fim, energia estimada e tarifa aplicada.
    2) Comandos automatizados: o sistema decide sozinho, sem
       intervenção humana, quando ativar a tarifa verde e como
       redistribuir a potência entre veículos (peak shaving).
    3) Encerramento automático de sessão por tempo máximo
       simulado (demonstra automação de ponta a ponta).
    4) Coleta e exibição de dados funcionais via Monitor Serial:
       status em tempo real, lista de sessões ativas e um
       relatório consolidado (energia, receita, economia verde).

  HARDWARE / LIGACOES (Tinkercad):
    - Arduino Uno R3
    - LED Vaga 1........... pino digital 13
    - LED Vaga 2........... pino digital 12
    - LED Vaga 3........... pino digital 11
    - LED Modo Solar (NOVO) pino digital 10
        -> Aceso automaticamente quando o sistema entra em
           modo de tarifa verde (geração solar >= 50%).
           Este é o "atuador" que evidencia a automação:
           ninguém aperta botão nenhum, o próprio sistema liga
           o LED com base na leitura do sensor solar.
    - Placa/sensor solar... porta analógica A0
    - Monitor Serial (9600 baud) como interface de
      entrada/saída, simulando o painel de gestão da estação.

  COMANDOS (digite no Monitor Serial e pressione Enter):
    1 / 2 / 3  -> liga ou desliga a sessão de recarga na vaga
                  correspondente (toggle)
    +          -> inicia sessão na primeira vaga livre
    -          -> encerra a sessão ativa mais antiga
    s          -> mostra status atual (solar, tarifa, potência)
    l          -> lista as sessões ativas no momento
    r          -> gera o relatório consolidado (dados funcionais)
    h          -> mostra o menu de ajuda
  ============================================================
*/

// ---------------------- CONFIGURAÇÃO DE HARDWARE ----------------------
const int NUM_VAGAS = 3;
const int PIN_LED_VAGA[NUM_VAGAS] = {13, 12, 11};
const int PIN_LED_SOLAR = 10;
const int PIN_SENSOR_SOLAR = A0;

// ---------------------- PARÂMETROS DE ENGENHARIA -----------------------
const float POTENCIA_TOTAL_MAX_KW   = 100.0;  // trava de segurança da rede
const float POTENCIA_MAX_POR_VEIC_KW = 50.0;  // teto individual por veículo
const float LIMIAR_SOLAR_VERDE_PCT  = 50.0;   // acima disso, tarifa verde

// Tarifas simuladas (R$/kWh) apenas para fins de demonstração funcional
const float TARIFA_NORMAL = 2.50;
const float TARIFA_VERDE  = 1.75; // ~30% de desconto no modo solar

// Tempo máximo de uma sessão simulada antes do encerramento AUTOMÁTICO
// (reduzido para facilitar a demonstração em vídeo; em um cenário real
// representaria o tempo de uma recarga completa)
const unsigned long TEMPO_MAX_SESSAO_MS = 30000UL; // 30 segundos

// ---------------------- ESTRUTURAS DE DADOS ----------------------------
struct SessaoRecarga {
  bool ativa;
  unsigned long inicioMs;
  float solarNoInicio;
  float potenciaAlocadaKW;
  bool tarifaVerde;
};

struct RegistroConcluido {
  int vaga;
  float duracaoS;
  float energiaKWh;
  bool tarifaVerde;
  float valorCobradoR;
};

SessaoRecarga vagas[NUM_VAGAS];

const int MAX_HISTORICO = 10;
RegistroConcluido historico[MAX_HISTORICO];
int totalHistorico = 0;       // quantos registros já existem (até MAX_HISTORICO)
int proximoIndiceHistorico = 0; // índice circular de escrita

// ---------------------- ESTADO GLOBAL DO SISTEMA ------------------------
bool modoVerdeAtivo = false;   // controlado automaticamente pelo sensor
int totalSessoesFinalizadas = 0;
int totalSessoesVerdes = 0;
float energiaTotalKWh = 0.0;
float receitaTotalR = 0.0;
float economiaTotalR = 0.0;

// =======================================================================
void setup() {
  Serial.begin(9600);

  for (int i = 0; i < NUM_VAGAS; i++) {
    pinMode(PIN_LED_VAGA[i], OUTPUT);
    digitalWrite(PIN_LED_VAGA[i], LOW);
    vagas[i].ativa = false;
  }
  pinMode(PIN_LED_SOLAR, OUTPUT);
  digitalWrite(PIN_LED_SOLAR, LOW);

  Serial.println(F("============================================="));
  Serial.println(F(" ESTACAO LIGHTWEEK - Sprint 3 (PoC Funcional)"));
  Serial.println(F("============================================="));
  imprimirAjuda();
}

// =======================================================================
void loop() {
  // 1) Leitura contínua do sensor solar (entrada analógica)
  float solarPct = lerSolarPercentual();

  // 2) COMANDO AUTOMATIZADO #1: alternância de tarifa verde
  //    O sistema decide sozinho, a cada ciclo, sem qualquer
  //    comando do usuário, se deve entrar/sair do modo solar.
  bool deveEstarVerde = (solarPct >= LIMIAR_SOLAR_VERDE_PCT);
  if (deveEstarVerde != modoVerdeAtivo) {
    modoVerdeAtivo = deveEstarVerde;
    digitalWrite(PIN_LED_SOLAR, modoVerdeAtivo ? HIGH : LOW);
    Serial.println();
    if (modoVerdeAtivo) {
      Serial.println(F("[AUTO] Geracao solar >= 50%. Tarifa VERDE ativada automaticamente."));
    } else {
      Serial.println(F("[AUTO] Geracao solar < 50%. Tarifa verde desativada automaticamente."));
    }
  }

  // 3) COMANDO AUTOMATIZADO #2: encerramento por tempo máximo
  //    Cada vaga é verificada a cada ciclo; se ultrapassar o
  //    tempo simulado de recarga, o sistema encerra sozinho.
  for (int i = 0; i < NUM_VAGAS; i++) {
    if (vagas[i].ativa && (millis() - vagas[i].inicioMs >= TEMPO_MAX_SESSAO_MS)) {
      Serial.println();
      Serial.print(F("[AUTO] Vaga "));
      Serial.print(i + 1);
      Serial.println(F(": tempo maximo de recarga atingido. Encerramento automatico."));
      encerrarSessao(i);
    }
  }

  // 4) Processa comando do usuário (se houver, via Monitor Serial)
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    processarComando(cmd, solarPct);
  }

  delay(50); // pequeno intervalo para não saturar o loop
}

// =======================================================================
// ---------------------- FUNÇÕES DE SENSOR / POTÊNCIA --------------------
float lerSolarPercentual() {
  int leitura = analogRead(PIN_SENSOR_SOLAR); // 0 a 1023
  return leitura * 100.0 / 1023.0;
}

int contarVagasOcupadas() {
  int n = 0;
  for (int i = 0; i < NUM_VAGAS; i++) if (vagas[i].ativa) n++;
  return n;
}

// COMANDO AUTOMATIZADO #3: redistribuição de potência (peak shaving)
// Chamada sempre que uma vaga muda de estado; ninguém define a
// potência manualmente, o algoritmo recalcula sozinho.
float calcularPotenciaPorVeiculo() {
  int ocupadas = contarVagasOcupadas();
  if (ocupadas == 0) return 0.0;
  float divisaoIgualitaria = POTENCIA_TOTAL_MAX_KW / ocupadas;
  return min(POTENCIA_MAX_POR_VEIC_KW, divisaoIgualitaria);
}

// =======================================================================
// ---------------------- CONTROLE DE SESSÕES ------------------------------
int encontrarVagaLivre() {
  for (int i = 0; i < NUM_VAGAS; i++) if (!vagas[i].ativa) return i;
  return -1;
}

int encontrarVagaMaisAntiga() {
  int idx = -1;
  unsigned long menorTempo = 0xFFFFFFFF;
  for (int i = 0; i < NUM_VAGAS; i++) {
    if (vagas[i].ativa && vagas[i].inicioMs < menorTempo) {
      menorTempo = vagas[i].inicioMs;
      idx = i;
    }
  }
  return idx;
}

void iniciarSessao(int vaga) {
  if (vaga < 0 || vaga >= NUM_VAGAS || vagas[vaga].ativa) return;

  vagas[vaga].ativa = true;
  vagas[vaga].inicioMs = millis();
  vagas[vaga].solarNoInicio = lerSolarPercentual();
  vagas[vaga].tarifaVerde = modoVerdeAtivo;

  digitalWrite(PIN_LED_VAGA[vaga], HIGH);

  // redistribui potência entre todas as vagas ativas (automatizado)
  float potenciaPorVeiculo = calcularPotenciaPorVeiculo();
  for (int i = 0; i < NUM_VAGAS; i++) {
    if (vagas[i].ativa) vagas[i].potenciaAlocadaKW = potenciaPorVeiculo;
  }

  Serial.println();
  Serial.print(F(">> Sessao iniciada na vaga "));
  Serial.println(vaga + 1);
  Serial.print(F("   Potencia alocada: "));
  Serial.print(potenciaPorVeiculo);
  Serial.println(F(" kW"));
  Serial.print(F("   Tarifa: "));
  Serial.println(vagas[vaga].tarifaVerde ? F("VERDE (solar)") : F("NORMAL"));
}

void encerrarSessao(int vaga) {
  if (vaga < 0 || vaga >= NUM_VAGAS || !vagas[vaga].ativa) return;

  unsigned long duracaoMs = millis() - vagas[vaga].inicioMs;
  float duracaoS = duracaoMs / 1000.0;

  // Energia estimada = potência alocada (kW) x tempo (h)
  // Não há sensor de corrente físico no protótipo, então a energia é
  // derivada da potência alocada pelo algoritmo de peak shaving.
  float energiaKWh = vagas[vaga].potenciaAlocadaKW * (duracaoMs / 3600000.0);
  float tarifaAplicada = vagas[vaga].tarifaVerde ? TARIFA_VERDE : TARIFA_NORMAL;
  float valorCobrado = energiaKWh * tarifaAplicada;
  float economia = vagas[vaga].tarifaVerde ? energiaKWh * (TARIFA_NORMAL - TARIFA_VERDE) : 0.0;

  // registra no histórico circular
  RegistroConcluido r;
  r.vaga = vaga + 1;
  r.duracaoS = duracaoS;
  r.energiaKWh = energiaKWh;
  r.tarifaVerde = vagas[vaga].tarifaVerde;
  r.valorCobradoR = valorCobrado;
  historico[proximoIndiceHistorico] = r;
  proximoIndiceHistorico = (proximoIndiceHistorico + 1) % MAX_HISTORICO;
  if (totalHistorico < MAX_HISTORICO) totalHistorico++;

  // atualiza agregados (dados funcionais do relatório)
  totalSessoesFinalizadas++;
  if (r.tarifaVerde) totalSessoesVerdes++;
  energiaTotalKWh += energiaKWh;
  receitaTotalR += valorCobrado;
  economiaTotalR += economia;

  // libera a vaga
  vagas[vaga].ativa = false;
  digitalWrite(PIN_LED_VAGA[vaga], LOW);

  Serial.println();
  Serial.print(F("<< Sessao encerrada na vaga "));
  Serial.println(vaga + 1);
  Serial.print(F("   Duracao: "));
  Serial.print(duracaoS);
  Serial.println(F(" s"));
  Serial.print(F("   Energia estimada: "));
  Serial.print(energiaKWh, 4);
  Serial.println(F(" kWh"));
  Serial.print(F("   Valor cobrado: R$ "));
  Serial.println(valorCobrado, 2);
  if (r.tarifaVerde) {
    Serial.print(F("   Economia (tarifa verde): R$ "));
    Serial.println(economia, 2);
  }

  // redistribui potência entre as vagas que sobraram ativas (automatizado)
  float potenciaPorVeiculo = calcularPotenciaPorVeiculo();
  for (int i = 0; i < NUM_VAGAS; i++) {
    if (vagas[i].ativa) vagas[i].potenciaAlocadaKW = potenciaPorVeiculo;
  }
}

// =======================================================================
// ---------------------- COMANDOS DO USUÁRIO ------------------------------
void processarComando(char cmd, float solarPct) {
  switch (cmd) {
    case '1': case '2': case '3': {
      int vaga = cmd - '1';
      if (vagas[vaga].ativa) encerrarSessao(vaga);
      else iniciarSessao(vaga);
      break;
    }
    case '+': {
      int livre = encontrarVagaLivre();
      if (livre == -1) Serial.println(F("Estacao lotada: nenhuma vaga livre."));
      else iniciarSessao(livre);
      break;
    }
    case '-': {
      int maisAntiga = encontrarVagaMaisAntiga();
      if (maisAntiga == -1) Serial.println(F("Nenhuma sessao ativa para encerrar."));
      else encerrarSessao(maisAntiga);
      break;
    }
    case 's': imprimirStatus(solarPct); break;
    case 'l': listarSessoesAtivas(); break;
    case 'r': imprimirRelatorio(); break;
    case 'h': imprimirAjuda(); break;
    default: break; // ignora '\n', '\r' e teclas não mapeadas
  }
}

// =======================================================================
// ---------------------- SAÍDA / EXIBIÇÃO DE DADOS ------------------------
void imprimirStatus(float solarPct) {
  Serial.println();
  Serial.println(F("----- STATUS DA ESTACAO -----"));
  Serial.print(F("Geracao solar atual : "));
  Serial.print(solarPct, 1);
  Serial.println(F(" %"));
  Serial.print(F("Tarifa em vigor      : "));
  Serial.println(modoVerdeAtivo ? F("VERDE (desconto solar)") : F("NORMAL"));
  Serial.print(F("Vagas ocupadas       : "));
  Serial.print(contarVagasOcupadas());
  Serial.print(F(" de "));
  Serial.println(NUM_VAGAS);
  Serial.print(F("Potencia por veiculo : "));
  Serial.print(calcularPotenciaPorVeiculo());
  Serial.println(F(" kW"));
}

void listarSessoesAtivas() {
  Serial.println();
  Serial.println(F("----- SESSOES ATIVAS -----"));
  bool alguma = false;
  for (int i = 0; i < NUM_VAGAS; i++) {
    if (vagas[i].ativa) {
      alguma = true;
      float decorridoS = (millis() - vagas[i].inicioMs) / 1000.0;
      Serial.print(F("Vaga ")); Serial.print(i + 1);
      Serial.print(F(" | ")); Serial.print(decorridoS, 0); Serial.print(F("s decorridos"));
      Serial.print(F(" | ")); Serial.print(vagas[i].potenciaAlocadaKW); Serial.print(F(" kW"));
      Serial.print(F(" | ")); Serial.println(vagas[i].tarifaVerde ? F("VERDE") : F("NORMAL"));
    }
  }
  if (!alguma) Serial.println(F("Nenhuma sessao ativa no momento."));
}

void imprimirRelatorio() {
  Serial.println();
  Serial.println(F("========= RELATORIO CONSOLIDADO ========="));
  Serial.print(F("Sessoes concluidas total : "));
  Serial.println(totalSessoesFinalizadas);
  Serial.print(F("  - com tarifa verde     : "));
  Serial.println(totalSessoesVerdes);
  Serial.print(F("Energia total entregue   : "));
  Serial.print(energiaTotalKWh, 4);
  Serial.println(F(" kWh"));
  Serial.print(F("Receita total            : R$ "));
  Serial.println(receitaTotalR, 2);
  Serial.print(F("Economia gerada (verde)  : R$ "));
  Serial.println(economiaTotalR, 2);

  Serial.println(F("--- Historico (mais recentes) ---"));
  if (totalHistorico == 0) {
    Serial.println(F("Nenhuma sessao concluida ainda."));
  } else {
    for (int i = 0; i < totalHistorico; i++) {
      RegistroConcluido r = historico[i];
      Serial.print(F("Vaga ")); Serial.print(r.vaga);
      Serial.print(F(" | ")); Serial.print(r.duracaoS, 0); Serial.print(F("s"));
      Serial.print(F(" | ")); Serial.print(r.energiaKWh, 4); Serial.print(F(" kWh"));
      Serial.print(F(" | ")); Serial.print(r.tarifaVerde ? F("VERDE") : F("NORMAL"));
      Serial.print(F(" | R$ ")); Serial.println(r.valorCobradoR, 2);
    }
  }
  Serial.println(F("==========================================="));
}

void imprimirAjuda() {
  Serial.println();
  Serial.println(F("Comandos disponiveis:"));
  Serial.println(F("  1/2/3  -> liga/desliga sessao na vaga correspondente"));
  Serial.println(F("  +      -> inicia sessao na 1a vaga livre"));
  Serial.println(F("  -      -> encerra a sessao ativa mais antiga"));
  Serial.println(F("  s      -> status atual (solar, tarifa, potencia)"));
  Serial.println(F("  l      -> lista sessoes ativas"));
  Serial.println(F("  r      -> relatorio consolidado (dados funcionais)"));
  Serial.println(F("  h      -> este menu de ajuda"));
  Serial.println();
}

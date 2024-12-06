#include <SPI.h>
#include <MFRC522.h>
#include <Dabble.h>

#define CUSTOM_SETTINGS
#define INCLUDE_TERMINAL_MODULE

#define RST_PIN 9
#define SS_PIN 10
#define BUZZER_PIN 4
#define GREEN_LED 5
#define RED_LED 6

MFRC522 rfid(SS_PIN, RST_PIN);

const byte MASTER_UID[] = {0x4F, 0xC1, 0x77, 0x99}; // UID do cartão mestre
bool modoGravacao = false;

bool piscarGreenLedStatus = false;    // Estado atual do LED (ligado/desligado)
unsigned long ultimoPiscarGreen = 0; // Último tempo em que o LED mudou de estado
const unsigned long intervaloPiscarGreen = 500; // Intervalo de tempo para piscar (em ms)

bool piscarRedLedStatus = false;    // Estado atual do LED vermelho (ligado/desligado)
unsigned long ultimoPiscarRed = 0; // Último tempo em que o LED vermelho mudou de estado
const unsigned long intervaloPiscarRed = 500; // Intervalo de tempo para piscar (em ms)

void setup() {
  Serial.begin(9600);  // Comunicação serial
  Dabble.begin(9600);  // Inicializa Bluetooth com o mesmo baudrate
  
  SPI.begin();         // Inicializa SPI
  rfid.PCD_Init();     // Inicializa o RFID
  delay(50);
  rfid.PCD_DumpVersionToSerial();

  pinMode(BUZZER_PIN, OUTPUT); // Configura o pino do buzzer como saída
  digitalWrite(BUZZER_PIN, LOW); // Garante que o buzzer esteja desligado inicialmente

  pinMode(GREEN_LED , OUTPUT);
  pinMode(RED_LED , OUTPUT);

  digitalWrite(RED_LED, LOW);

  while (!Serial);

  Serial.println("Sistema inicializado.");
  digitalWrite(GREEN_LED , HIGH);
}

void loop() {
  Dabble.processInput(); // Atualiza dados do Bluetooth

  if (!modoGravacao) {
    verificaCartao();
  } else {
    piscarLedVermelho();
    processaGravacao();
  }

  if (!rfid.PICC_IsNewCardPresent() && !rfid.PICC_ReadCardSerial()) {
    SPI.end();
    delay(10);
    SPI.begin();
    rfid.PCD_Init();
    Serial.println("SPI e RFID reiniciados.");
  }

  delay(500);
}

void piscarLedVermelho() {
  if (millis() - ultimoPiscarRed >= intervaloPiscarRed) {
    ultimoPiscarRed = millis();
    piscarRedLedStatus = !piscarRedLedStatus; // Alterna o estado do LED
    digitalWrite(RED_LED, piscarRedLedStatus ? HIGH : LOW);
  }
}

void acionarBuzzer(int duracao) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(duracao);
  digitalWrite(BUZZER_PIN, LOW);
}

void verificaCartao() {
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    acionarBuzzer(100);  // Aciona o buzzer por 100 ms para qualquer cartão detectado

    if (memcmp(rfid.uid.uidByte, MASTER_UID, sizeof(MASTER_UID)) == 0) {
      Serial.println("Cartão mestre detectado. Modo gravação ativado.");
      acionarBuzzer(500);  // Aciona o buzzer por 500 ms para o cartão mestre
      modoGravacao = true;
    } else {
      lerDados();  // Aciona o buzzer também na função lerDados()
    }

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }
}

void lerDados() {
  byte buffer[18];
  byte bufferSize = sizeof(buffer);
  String data = "";

  for (int block = 1; block <= 2; block++) {
    if (readBlock(block, buffer, bufferSize)) {
      data += String((char *)buffer);
    } else {
      Serial.println("Erro ao ler dados.");
      acionarBuzzer(200);  // Aciona o buzzer para erro de leitura
      return;
    }
  }

  // Envia os dados no formato esperado pelo Python
  Serial.print("DADO:");
  Serial.println(data);

  acionarBuzzer(300);  // Aciona o buzzer após leitura bem-sucedida
}

bool readBlock(byte block, byte *buffer, byte &bufferSize) {
  MFRC522::StatusCode status;
  MFRC522::MIFARE_Key key;
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

  status = rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(rfid.uid));
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Erro na autenticação do bloco ");
    Serial.println(block);
    return false;
  }

  status = rfid.MIFARE_Read(block, buffer, &bufferSize);
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Erro ao ler o bloco ");
    Serial.println(block);
    return false;
  }
  return true;
}

void processaGravacao() {
  static bool mensagemExibida = false;

  if (!mensagemExibida) {
    Terminal.print("Digite os dados no formato: Nome,RA");
    mensagemExibida = true;
  }

  if (Terminal.available()) {
    String input = Terminal.readString();
    input.trim();
    mensagemExibida = false;

    if (input.length() > 32) {
      Terminal.print("Erro: As informações devem ter no máximo 32 caracteres!");
      acionarBuzzer(200);  // Aciona o buzzer para erro
      return;
    }

    Terminal.print("Aproxime o cartão para gravar...");

    while (true) {
      // Piscar LED verde enquanto espera
      if (millis() - ultimoPiscarGreen >= intervaloPiscarGreen) {
        ultimoPiscarGreen = millis();
        piscarGreenLedStatus = !piscarGreenLedStatus; // Inverte o estado do LED
        digitalWrite(GREEN_LED, piscarGreenLedStatus ? HIGH : LOW);
      }

      if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
        if (memcmp(rfid.uid.uidByte, MASTER_UID, sizeof(MASTER_UID)) == 0) {
          Terminal.print("Cartão mestre não pode ser gravado. Use outro cartão.");
          Serial.println("Cartão mestre detectado. Use outro cartão.");
          acionarBuzzer(200);  // Aciona o buzzer para erro
          rfid.PICC_HaltA();
          continue;
        }

        byte dataBlock1[16], dataBlock2[16];
        memset(dataBlock1, 0, 16);
        memset(dataBlock2, 0, 16);
        input.getBytes(dataBlock1, 16);
        if (input.length() > 16) {
          input.substring(16).getBytes(dataBlock2, 16);
        }

        for (int block = 1; block <= 2; block++) {
          byte *dataBlock = (block == 1) ? dataBlock1 : dataBlock2;
          if (!writeBlock(block, dataBlock)) {
            acionarBuzzer(200);  // Aciona o buzzer para erro
            return;
          }
        }

        Terminal.print("Informações gravadas com sucesso!");
        Serial.println("Informações gravadas com sucesso!");
        acionarBuzzer(300);  // Aciona o buzzer para sucesso na gravação
        rfid.PICC_HaltA();
        rfid.PCD_StopCrypto1();
        modoGravacao = false;

        // Desliga o LED vermelho e reseta o estado de piscar
        digitalWrite(RED_LED, LOW);
        piscarRedLedStatus = false;

        digitalWrite(GREEN_LED, HIGH); // Deixe o LED aceso após gravar
        return;
      }
    }
  }
}

bool writeBlock(byte block, byte *dataBlock) {
  MFRC522::StatusCode status;
  MFRC522::MIFARE_Key key;
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

  status = rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(rfid.uid));
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Erro na autenticação do bloco");
    Serial.println(block);
    return false;
  }

  status = rfid.MIFARE_Write(block, dataBlock, 16);
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Erro ao gravar no bloco");
    Serial.println(block);
    return false;
  }
  Serial.print("Bloco ");
  Serial.print(block);
  Serial.println("gravado com sucesso.");
  return true;
}
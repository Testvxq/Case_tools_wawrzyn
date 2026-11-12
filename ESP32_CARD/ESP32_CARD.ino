
#include <WiFi.h>
#include <HTTPClient.h>
#include <Wiegand.h>
#include <Preferences.h>

// WIFI
const char* AP_SSID = "ESP32_DEV";
const char* AP_PWD  = "12345678";

// ADRESY
const char* OPEN_URL  = "http://192.168.4.1/close";
const char* CLOSE_URL = "http://192.168.4.1/open";

// WIEGAND
WIEGAND wg;
#define WIEGAND_D0 18
#define WIEGAND_D1 19

// PRZYCISK
#define BUTTON_PIN 13

// KARTA POCZATKOWA
#define DEFAULT_CARD 5984411ULL

// PAMIEC KART
Preferences prefs;

#define MAX_CARDS 100

uint64_t cards[MAX_CARDS];
int cardCount = 0;

// TRYBY
bool addMode = false;
bool removeMode = false;

// OBSLUGA PRZYCISKU
bool lastRawButton = HIGH;
bool stableButton = HIGH;

unsigned long debounceTime = 0;
const unsigned long DEBOUNCE_DELAY = 40;

unsigned long pressStartTime = 0;
unsigned long lastReleaseTime = 0;

const unsigned long LONG_PRESS_TIME = 3000;
const unsigned long SEQUENCE_TIMEOUT = 1200;

int pressCount = 0;
bool modeTriggered = false;

// ------------------------------------------------
// WIFI / HTTP
// ------------------------------------------------

void sendRequest(const char* url) {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Brak WiFi!");
    return;
  }

  HTTPClient http;

  http.begin(url);
  http.setTimeout(3000);

  int response = http.GET();

  Serial.print("HTTP: ");
  Serial.println(response);

  http.end();
}

// ------------------------------------------------
// PAMIEC KART
// ------------------------------------------------

void saveCards() {

  if (cardCount > 0) {
    prefs.putBytes("cards", cards,
                   cardCount * sizeof(uint64_t));
  } else {
    prefs.remove("cards");
  }

  Serial.println("Zapisano pamiec kart");
}

void loadCards() {

  prefs.begin("locker", false);

  // Pierwsze uruchomienie
  if (!prefs.getBool("initialized", false)) {

    cards[0] = DEFAULT_CARD;
    cardCount = 1;

    saveCards();

    prefs.putBool("initialized", true);

    Serial.println("Dodano karte poczatkowa");
  }

  // Wczytanie kart z pamieci
  size_t storedSize = prefs.getBytesLength("cards");

  if (storedSize > 0 &&
      storedSize <= sizeof(cards) &&
      storedSize % sizeof(uint64_t) == 0) {

    cardCount = storedSize / sizeof(uint64_t);

    prefs.getBytes("cards", cards, storedSize);

  } else if (storedSize > 0) {

    Serial.println("Blad pamieci kart");
    cardCount = 0;
  }

  Serial.print("Liczba zapisanych kart: ");
  Serial.println(cardCount);
}

bool isCardSaved(uint64_t code) {

  for (int i = 0; i < cardCount; i++) {

    if (cards[i] == code) {
      return true;
    }
  }

  return false;
}

void addCard(uint64_t code) {

  if (isCardSaved(code)) {
    Serial.println("Karta juz istnieje");
    return;
  }

  if (cardCount >= MAX_CARDS) {
    Serial.println("Pamiec kart pelna!");
    return;
  }

  cards[cardCount] = code;
  cardCount++;

  saveCards();

  Serial.print("Dodano karte: ");
  Serial.println((unsigned long long)code);
}

void removeCard(uint64_t code) {

  for (int i = 0; i < cardCount; i++) {

    if (cards[i] == code) {

      for (int j = i; j < cardCount - 1; j++) {
        cards[j] = cards[j + 1];
      }

      cardCount--;

      saveCards();

      Serial.print("Usunieto karte: ");
      Serial.println((unsigned long long)code);

      return;
    }
  }

  Serial.println("Karty nie ma w pamieci");
}

// ------------------------------------------------
// OBSLUGA KART
// ------------------------------------------------

void checkCard() {

  if (!wg.available()) {
    return;
  }

  uint64_t code = wg.getCode();

  Serial.print("Odczytana karta: ");
  Serial.println((unsigned long long)code);

  // TRYB DODAWANIA
  if (addMode) {

    addCard(code);

    // Nowo dodana karta moze od razu otworzyc
    if (isCardSaved(code)) {
      sendRequest(OPEN_URL);
    }

    return;
  }

  // TRYB USUWANIA
  if (removeMode) {

    removeCard(code);

    return;
  }

  // NORMALNY TRYB
  if (isCardSaved(code)) {

    Serial.println("KARTA OK - OPEN");

    sendRequest(OPEN_URL);

  } else {

    Serial.println("BRAK DOSTEPU");
  }
}

// ------------------------------------------------
// SEKWENCJE PRZYCISKU
// ------------------------------------------------

// Zwykle klikniecia zostaja wykonane dopiero
// po uplywie czasu na rozpoznanie sekwencji.

void executePendingClicks() {

  for (int i = 0; i < pressCount; i++) {

    Serial.println("CLOSE");

    sendRequest(CLOSE_URL);
  }

  pressCount = 0;
}

// Rozpoczecie nacisniecia
void buttonPressed() {

  if (addMode || removeMode) {
    return;
  }

  pressCount++;

  pressStartTime = millis();

  Serial.print("Klikniecie: ");
  Serial.println(pressCount);
}

// Puszczenie przycisku
void buttonReleased() {

  if (addMode || removeMode) {
    return;
  }

  if (!modeTriggered) {
    lastReleaseTime = millis();
  }

}

// ------------------------------------------------
// ROZPOZNAWANIE TRYBOW
// ------------------------------------------------

void checkButton() {

  bool reading = digitalRead(BUTTON_PIN);

  // Debounce
  if (reading != lastRawButton) {
    debounceTime = millis();
  }

  if (millis() - debounceTime > DEBOUNCE_DELAY) {

    if (reading != stableButton) {

      stableButton = reading;

      if (stableButton == LOW) {
        buttonPressed();
      } else {
        buttonReleased();
        modeTriggered = false;
      }
    }
  }

  lastRawButton = reading;

  // Sprawdzanie dlugiego przytrzymania
  if (stableButton == LOW && !modeTriggered &&
      !addMode && !removeMode) {

    if (millis() - pressStartTime >= LONG_PRESS_TIME) {

      // 5 klikniec = DODAWANIE
      if (pressCount == 5) {

        addMode = true;
        modeTriggered = true;
        pressCount = 0;

        Serial.println("==================");
        Serial.println("TRYB DODAWANIA KART");
        Serial.println("==================");
      }

      // 8 klikniec = USUWANIE
      else if (pressCount == 8) {

        removeMode = true;
        modeTriggered = true;
        pressCount = 0;

        Serial.println("==================");
        Serial.println("TRYB USUWANIA KART");
        Serial.println("==================");
      }
    }
  }

  // Zwykle klikniecia - zamkniecie
  if (stableButton == HIGH &&
      pressCount > 0 &&
      millis() - lastReleaseTime > SEQUENCE_TIMEOUT) {

    executePendingClicks();
  }
}

// ------------------------------------------------
// SETUP
// ------------------------------------------------

void setup() {

  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // WIFI
  WiFi.mode(WIFI_STA);
  WiFi.begin(AP_SSID, AP_PWD);

  Serial.println("Laczenie z WiFi...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi polaczone!");

  Serial.println(WiFi.localIP());

  // PAMIEC
  loadCards();

  // WIEGAND
  wg.begin(WIEGAND_D0, WIEGAND_D1);

  Serial.println("SYSTEM GOTOWY");
}

// ------------------------------------------------
// LOOP
// ------------------------------------------------

void loop() {

  checkButton();

  checkCard();

}

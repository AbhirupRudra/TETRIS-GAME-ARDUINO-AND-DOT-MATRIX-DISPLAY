#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#define FAST_DROP_DELAY 100  // Time between fast drop movements

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4    // 4 modules arranged horizontally (32x8 display)
#define DATA_PIN 11
#define CLK_PIN 13
#define CS_PIN 10

#define BTN_UP    4
#define BTN_DOWN  5
#define BTN_RIGHT 2
#define BTN_LEFT  3
#define BUZZER    A0

// Sound frequencies
#define MOVE_SOUND 100
#define ROTATE_SOUND 200
#define DROP_SOUND 300
#define LINE_SOUND 523

// Button debouncing
#define DEBOUNCE_DELAY 50
unsigned long lastDebounceTime = 0;
byte lastButtonState = HIGH;
byte buttonState = HIGH;

MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
MD_Parola matrix = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

// Tetromino shapes with fixed I-shape (no rotation)
const byte shapes[7][4][4] = {
  // L shape (4 rotations)
  {
    {0b10000000, 0b10000000, 0b11000000, 0b00000000}, // 0Â°
    {0b11100000, 0b10000000, 0b00000000, 0b00000000}, // 90Â°
    {0b11000000, 0b01000000, 0b01000000, 0b00000000}, // 180Â°
    {0b00100000, 0b11100000, 0b00000000, 0b00000000}  // 270Â°
  },
  // J shape (4 rotations)
  {
    {0b01000000, 0b01000000, 0b11000000, 0b00000000}, // 0Â°
    {0b10000000, 0b11100000, 0b00000000, 0b00000000}, // 90Â°
    {0b11000000, 0b10000000, 0b10000000, 0b00000000}, // 180Â°
    {0b11100000, 0b00100000, 0b00000000, 0b00000000}  // 270Â°
  },
  // O shape (same for all rotations)
  {
    {0b11000000, 0b11000000, 0b00000000, 0b00000000},
    {0b11000000, 0b11000000, 0b00000000, 0b00000000},
    {0b11000000, 0b11000000, 0b00000000, 0b00000000},
    {0b11000000, 0b11000000, 0b00000000, 0b00000000}
  },
  // T shape (4 rotations)
  {
    {0b11100000, 0b01000000, 0b00000000, 0b00000000}, // 0Â°
    {0b01000000, 0b11000000, 0b01000000, 0b00000000}, // 90Â°
    {0b01000000, 0b11100000, 0b00000000, 0b00000000}, // 180Â°
    {0b10000000, 0b11000000, 0b10000000, 0b00000000}  // 270Â°
  },
  // S shape (4 rotations)
  {
    {0b01100000, 0b11000000, 0b00000000, 0b00000000}, // 0Â°
    {0b10000000, 0b11000000, 0b01000000, 0b00000000}, // 90Â°
    {0b01100000, 0b11000000, 0b00000000, 0b00000000}, // 180Â°
    {0b10000000, 0b11000000, 0b01000000, 0b00000000}  // 270Â°
  },
  // Z shape (4 rotations)
  {
    {0b11000000, 0b01100000, 0b00000000, 0b00000000}, // 0Â°
    {0b01000000, 0b11000000, 0b10000000, 0b00000000}, // 90Â°
    {0b11000000, 0b01100000, 0b00000000, 0b00000000}, // 180Â°
    {0b01000000, 0b11000000, 0b10000000, 0b00000000}  // 270Â°
  },
  // I shape (4 rotations)
  {
    {0b10000000, 0b10000000, 0b10000000, 0b10000000}, // 0Â° (vertical)
    {0b00000000, 0b00000000, 0b11110000, 0b00000000}, // 90Â° (horizontal)
    {0b10000000, 0b10000000, 0b10000000, 0b10000000}, // 180Â°
    {0b00000000, 0b00000000, 0b11110000, 0b00000000}  // 270Â°
  }
};
struct Block {
  int x;        // Column (0-31)
  int y;        // Row (0-7)
  int shape;    // Shape index (0-6)
  int rotation; // Current rotation (0-3)
};

Block current;
bool grid[32][8] = {false}; // Playfield grid [columns][rows]
int speed = 500;
int score = 0;
bool gameOver = false;
bool welcome = false;
unsigned long lastMove = 0;
bool showScore = false;
unsigned long scoreTime = 0;
bool displayDirty = true;

void setup() {
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  
  mx.begin();
  matrix.begin();
  mx.control(MD_MAX72XX::INTENSITY, 5);
  mx.control(MD_MAX72XX::SCANLIMIT, 7);
  mx.clear();
  
  spawnBlock();
  lastMove = millis();

  randomSeed(analogRead(4));
}

void playTone(int freq, int duration) {
  tone(BUZZER, freq, duration);
  delay(duration);
  noTone(BUZZER);
}

void loop() {
  if (!welcome) displayWelcome();

  unsigned long currentTime = millis();
  
  if (gameOver) {
  playTone(100, 1000);
  displayGameOver();
   //if (currentTime - lastMove > 3000) resetGame();
   // return;
  }

  if (showScore) {
    if (currentTime - scoreTime > 1000) {
      showScore = false;
      displayDirty = true;
    }
    return;
  }

  checkInput();
  
  if (currentTime - lastMove > speed) {
    moveRight();
    lastMove = currentTime;
    displayDirty = true;
  }
  
  if (displayDirty) {
    drawScreen();
    displayDirty = false;
  }
}

void spawnBlock() {
  current = {0, 3, random(7), 0}; // Start in the middle
  if (!canMove(current.x, current.y)) gameOver = true;
  displayDirty = true;
  playTone(MOVE_SOUND, 50);
}

bool canMove(int newX, int newY) {
  for (int r = 0; r < 4; r++) {
    byte row = getShapeRow(r);
    for (int c = 0; c < 4; c++) {
      if (row & (0b10000000 >> c)) {
        int x = newX + c;
        int y = newY + r;
        if (x >= 32 || y < 0 || y >= 8 || (x >= 0 && grid[x][y])) 
          return false;
      }
    }
  }
  return true;
}

byte getShapeRow(int row) {
  return shapes[current.shape][current.rotation][row];
}



void checkInput() {
  static bool downPressed = false;
  static unsigned long downPressTime = 0;
  unsigned long currentTime = millis();
  
  // Debounced button reading
  byte reading = (digitalRead(BTN_UP) == LOW)   ? 1 : 0;
  reading |= (digitalRead(BTN_DOWN) == LOW)  ? 2 : 0;
  reading |= (digitalRead(BTN_LEFT) == LOW)  ? 4 : 0;
  reading |= (digitalRead(BTN_RIGHT) == LOW) ? 8 : 0;

  if (reading != lastButtonState) {
    lastDebounceTime = currentTime;
  }

  if ((currentTime - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading != buttonState) {
      buttonState = reading;
      
      // UP - move up
      if (buttonState & 1 && canMove(current.x, current.y - 1)) {
        current.y--;
        displayDirty = true;
        playTone(MOVE_SOUND, 30);
      }
      else {
        downPressed = false;
      }
      // DOWN - start fast drop
      if (buttonState & 2 && canMove(current.x, current.y + 1)) {
       
        current.y++;
         displayDirty = true;
      } else {
        downPressed = false;
      }
      
      // LEFT - move left
      if (buttonState & 4 && canMove(current.x + 1, current.y)) {
        downPressed = true;
        downPressTime = currentTime;
        current.x++;
        displayDirty = true;
        
        playTone(MOVE_SOUND, 30);
      }
      
      // RIGHT - rotate
      if (buttonState & 8) {
        rotate();
        displayDirty = true;
        playTone(ROTATE_SOUND, 50);
      }
    }
  }
  
  // Handle continuous down movement
  if (downPressed && (currentTime - downPressTime) > FAST_DROP_DELAY) {
    if (buttonState & 4 && canMove(current.x + 1, current.y)) 
    {      
        downPressTime = currentTime;
        current.x++;
        displayDirty = true;
        
        playTone(MOVE_SOUND, 30);
      }
  }
  
  lastButtonState = reading;
}

void moveRight() {
  if (canMove(current.x + 1, current.y)) {
    current.x++;
    displayDirty = true;
    playTone(MOVE_SOUND, 30);
  } else {
    placeBlock();
    checkLines();
    spawnBlock();
    playTone(DROP_SOUND, 70);
  }
}

void placeBlock() {
  for (int r = 0; r < 4; r++) {
    byte row = getShapeRow(r);
    for (int c = 0; c < 4; c++) {
      if (row & (0b10000000 >> c)) {
        int x = current.x + c;
        int y = current.y + r;
        if (x >= 0 && x < 32 && y >= 0 && y < 8) {
          grid[x][y] = true;
        }
      }
    }
  }
  displayDirty = true;
}

void rotate() {
  int oldRotation = current.rotation;
  int newRotation = (current.rotation + 1) % 4;

  // Wall kick table for I-piece
  int kicks[4][5][2] = {
    {{0,0}, {-2,0}, {1,0}, {-2,-1}, {1,2}},  // 0 -> 1
    {{0,0}, {-1,0}, {2,0}, {-1,2}, {2,-1}},  // 1 -> 2
    {{0,0}, {2,0}, {-1,0}, {2,1}, {-1,-2}},  // 2 -> 3
    {{0,0}, {1,0}, {-2,0}, {1,-2}, {-2,1}}   // 3 -> 0
  };

  // Try each kick position
  for (int i = 0; i < 5; i++) {
    int newX = current.x + kicks[oldRotation][i][0];
    int newY = current.y + kicks[oldRotation][i][1];

    if (canMove(newX, newY)) {
      current.x = newX;
      current.y = newY;
      current.rotation = newRotation;
      return;
    }
  }

  // If no kick works, revert rotation
  current.rotation = oldRotation;
}


void checkLines() {
  int lines = 0;
  
  for (int x = 31; x >= 0; x--) {
    bool full = true;
    for (int y = 0; y < 8; y++) {
      if (!grid[x][y]) {
        full = false;
        break;
      }
    }
    
    if (full) {
      lines++;
      // Shift all lines to the left
      for (int nx = x; nx > 0; nx--) {
        for (int y = 0; y < 8; y++) {
          grid[nx][y] = grid[nx - 1][y];
        }
      }
      // Clear first column
      for (int y = 0; y < 8; y++) grid[0][y] = false;
      x++; // Check same column again
    }
  }
  
  if (lines > 0) {
    score += lines;
    if (speed > 100) speed -= 20;
    showScore = true;
    scoreTime = millis();
    //showScoreDisplay();
    playTone(LINE_SOUND, 200);
  }
  displayDirty = true;
}

void drawScreen() {
  mx.clear();
  
  // Draw grid (horizontal orientation)
  for (int x = 0; x < 32; x++) {
    for (int y = 0; y < 8; y++) {
      if (grid[x][y]) {
        // Calculate module and position within module
        int module = x / 8;
        int moduleCol = x % 8;
        mx.setPoint(7 - y, module * 8 + moduleCol, true);
      }
    }
  }
  
  // Draw current block
  for (int r = 0; r < 4; r++) {
    byte row = getShapeRow(r);
    for (int c = 0; c < 4; c++) {
      if (row & (0b10000000 >> c)) {
        int x = current.x + c;
        int y = current.y + r;
        if (x >= 0 && x < 32 && y >= 0 && y < 8) {
          int module = x / 8;
          int moduleCol = x % 8;
          mx.setPoint(7 - y, module * 8 + moduleCol, true);
        }
      }
    }
  }
  
  mx.update();
}


void displayGameOver() {
  mx.clear();
  matrix.displayClear();
  matrix.setTextAlignment(PA_CENTER);
  
  // matrix.displayText("GAME OVER", PA_CENTER, 100, 500, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  // while (!matrix.displayAnimate()) delay(50);
  // matrix.displayClear();
  // delay(1000);
  
  String scoreText = "Score: " + String(score);
  matrix.displayText(scoreText.c_str(), PA_CENTER, 100, 500, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  while (!matrix.displayAnimate()) delay(50);

  delay(1000);
  resetGame();
}

void displayWelcome() {
  mx.clear();
  matrix.displayClear();
  matrix.setTextAlignment(PA_CENTER);
  
  matrix.displayText("WELCOME TO TERRIS", PA_CENTER, 90, 500, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  while (!matrix.displayAnimate()) delay(50);
  
  welcome = true;

  matrix.displayClear();
  delay(1000);
}

void resetGame() {
  // Clear grid
  for (int x = 0; x < 32; x++) {
    for (int y = 0; y < 8; y++) {
      grid[x][y] = false;
    }
  }
  
  score = 0;
  speed = 500;
  gameOver = false;
  showScore = false;
  welcome = false;
  spawnBlock();
  lastMove = millis();
}
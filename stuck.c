/* Jogo modificado com base no jogo Climber Gamer da plataforma 8bitworkshop
*/

#include <stdlib.h>
#include <string.h>

// include NESLIB header
#include "neslib.h"

// include CC65 NES Header (PPU)
#include <nes.h>

// BCD arithmetic support
#include "bcd.h"
//#link "bcd.c"

// VRAM update buffer
#include "vrambuf.h"
//#link "vrambuf.c"

// link the pattern table into CHR ROM
//#link "chr_generic.s"

// famitone2 library
//#link "famitone2.s"

// music and sfx
//#link "music_dangerstreets.s"
extern char danger_streets_music_data[];
//#link "demosounds.s"
extern char demo_sounds[];

// indices of sound effects (0..3)
typedef enum { SND_START, SND_HIT, SND_COIN, SND_JUMP } SFXIndex;

///// DEFINES

#define COLS 30		// number of cols in the room
#define ROWS 60		// number of rows in the room


#define MAX_BULLETS 2		// max # of bullets in screen
#define MAX_X_ROOMS 2           // max x position of rooms
#define MAX_Y_ROOMS 2           // max x position of rooms
#define SCREEN_Y_BOTTOM 208	// bottom of screen in pixels
#define ACTOR_MIN_X 16		// leftmost position of actor
#define ACTOR_MAX_X 228		// rightmost position of actor
#define ACTOR_MIN_Y 9		// downmost position of actor
#define ACTOR_MAX_Y 184		// upmost position of actor
#define ACTOR_Y_CENTER 110      // Y center position

// constants for various tiles
#define CH_BORDER 0x40
#define CH_FLOOR 0xf4
#define CH_BLANK 0x20

///// GLOBALS

// vertical scroll amount in pixels
//static int scroll_pixel_yy = 0;

// vertical scroll amount in tiles (scroll_pixel_yy / 8)
//static byte scroll_tile_y = 0;

// score (BCD)
static byte score = 0;

// screen flash animation (virtual bright)
static byte vbright = 4;

// controls the time between events
int time_collisions = 0, time_bullet = 0;

// control the number of bullets in screen
int num_bullets = 0;

// control o state of game
byte state = 0;

// link title screen palette and RLE nametable
//#link "title.s"
extern const byte title_pal[16];
extern const byte title_rle[];

// victory and defeat texts
const char* GAME_OVER_TEXT = 
  "GAME OVER PRESS START";

const char* WIN_TEXT = 
  "YOU WIN PRESS START";

// random byte between (a ... b-1)
// use rand() because rand8() has a cycle of 255
byte rndint(byte a, byte b) {
  return (rand() % (b-a)) + a;
}

// return nametable address for tile (x,y)
// assuming vertical scrolling (horiz. mirroring)
word getntaddr(byte x, byte y) {
  word addr;
  if (y < 30) {
    addr = NTADR_A(x,y);	// nametable A
  } else {
    addr = NTADR_C(x,y-30);	// nametable C
  }
  return addr;
}

// convert nametable address to attribute address
word nt2attraddr(word a) {
  return (a & 0x2c00) | 0x3c0 |
    ((a >> 4) & 0x38) | ((a >> 2) & 0x07);
}

/// METASPRITES

// define a 2x2 metasprite
#define DEF_METASPRITE_2x2(name,code,pal)\
const unsigned char name[]={\
        0,      0,      (code)+0,   pal, \
        0,      8,      (code)+1,   pal, \
        8,      0,      (code)+2,   pal, \
        8,      8,      (code)+3,   pal, \
        128};

// define a 2x2 metasprite, flipped horizontally
#define DEF_METASPRITE_2x2_FLIP(name,code,pal)\
const unsigned char name[]={\
        8,      0,      (code)+0,   (pal)|OAM_FLIP_H, \
        8,      8,      (code)+1,   (pal)|OAM_FLIP_H, \
        0,      0,      (code)+2,   (pal)|OAM_FLIP_H, \
        0,      8,      (code)+3,   (pal)|OAM_FLIP_H, \
        128};

// right-facing
DEF_METASPRITE_2x2(playerRStand, 0xd8, 0);
DEF_METASPRITE_2x2(playerRRun1, 0xdc, 0);
DEF_METASPRITE_2x2(playerRRun2, 0xe0, 0);
DEF_METASPRITE_2x2(playerRRun3, 0xe4, 0);
DEF_METASPRITE_2x2(playerRJump, 0xe8, 0);
DEF_METASPRITE_2x2(playerRClimb, 0xec, 0);
DEF_METASPRITE_2x2(playerRSad, 0xf0, 0);

// left-facing
DEF_METASPRITE_2x2_FLIP(playerLStand, 0xd8, 0);
DEF_METASPRITE_2x2_FLIP(playerLRun1, 0xdc, 0);
DEF_METASPRITE_2x2_FLIP(playerLRun2, 0xe0, 0);
DEF_METASPRITE_2x2_FLIP(playerLRun3, 0xe4, 0);
DEF_METASPRITE_2x2_FLIP(playerLJump, 0xe8, 0);
DEF_METASPRITE_2x2_FLIP(playerLClimb, 0xec, 0);
DEF_METASPRITE_2x2_FLIP(playerLSad, 0xf0, 0);

// trap
DEF_METASPRITE_2x2(trap, 0xc8, 3);

// life
DEF_METASPRITE_2x2(life, 0xcc, 0);

// player run sequence
const unsigned char* const playerRunSeq[16] = {
  playerLRun1, playerLRun2, playerLRun3, 
  playerLRun1, playerLRun2, playerLRun3, 
  playerLRun1, playerLRun2,
  playerRRun1, playerRRun2, playerRRun3, 
  playerRRun1, playerRRun2, playerRRun3, 
  playerRRun1, playerRRun2,
};

///// ACTORS

typedef enum ActorState {
  STANDING, WALKING, WALKING2, DAMAGED};

typedef enum ActorType {
  ACTOR_PLAYER, ACTOR_ENEMY
};

typedef struct Actor {
  word yy;		// Y position in pixels (16 bit)
  byte x;		// X position in pixels (8 bit)
  byte roomx;		// room index x
  byte roomy;		// room index y
  byte state;		// ActorState
  int name:2;		// ActorType (2 bits)
  int pal:2;		// palette color (2 bits)
  int dir:1;		// direction (0=right, 1=left)
  int life;             // life of actor
} Actor;

Actor actor;	// actor

// struct definition for a single room
typedef struct Room {
  byte xpos;        // index x
  byte ypos;
  int numTraps;
  byte xt[7];
  byte yt[7];
  byte numMonsters;
  Actor monsters[5];
} Room;

Room rooms[MAX_X_ROOMS][MAX_Y_ROOMS]; // all rooms

typedef struct Bullet {
  word yy;		// Y position in pixels (16 bit)
  byte x;		// X position in pixels (8 bit)
  int state:1;		// State
  int pal:2;		// palette color (2 bits)
  byte m;
} Bullet;

Bullet bullets[MAX_BULLETS];	// all bullets

// draws the actor according to his current status
void draw_actor(struct Actor * a) {
  bool dir;
  const unsigned char* meta;
  byte x,y; // sprite variables
  // get screen Y position of actor
  int screen_y = SCREEN_Y_BOTTOM - a->yy;
  dir = a->dir;
  switch (a->state) {
    case STANDING:
      meta = dir ? playerLStand : playerRStand;
      break;
    case WALKING:
      meta = playerRunSeq[((a->x >> 1) & 7) + (dir?0:8)];
      break;
    case WALKING2:
      meta = playerRunSeq[((a->yy >> 1) & 7) + (dir?0:8)];
      break;
    case DAMAGED:
      meta = dir ? playerLSad : playerRSad;
      break;
  }
  // set sprite values, draw sprite
  x = a->x;
  y = screen_y;
  oam_meta_spr_pal(x, y, a->pal, meta);
  return;
}

// draw the bullet i
void draw_bullet(byte i) {
  struct Bullet* b = &bullets[i];
  if(bullets[i].state == 1) {
    int screen_y = SCREEN_Y_BOTTOM - bullets[i].yy + 0;
    oam_off = oam_spr(bullets[i].x, screen_y, 25, 2, oam_off);
  }
  return;
}

// draw the scoreboard, right now just two digits
void draw_scoreboard() {
  oam_off = oam_spr(24+0, 8, '0'+(score >> 4), 2, oam_off);
  oam_off = oam_spr(24+8, 8, '0'+(score & 0xf), 2, oam_off);
}

// draw the life of actor
void draw_life() {
  byte i;
  for (i = 0; i < actor.life; i++){
    oam_off = oam_spr(200+8*i, 8, 21, 3, oam_off);
  }
}

// draw a line of room
void draw_room_line(byte line) {
  char buf[COLS];	// nametable buffer
  char attrs[8];	// attribute buffer 
  byte rowy;		// row in nametable (0-59)
  word addr;		// nametable address
  byte i;		// loop counter

      if ((line == 0 || line == 25)) {
        // iterate through all 32 columns
        for (i=0; i<COLS; i+=2) {
          if (line) {
            if(actor.roomy < MAX_Y_ROOMS - 1 && i >= 12 && i <= 17) {
              buf[i] = 0;               // gap
              buf[i+1] = 0;             // gap
            }
            else { 
              buf[i] = CH_FLOOR;	// upper-left
              buf[i+1] = CH_FLOOR+2;	// upper-right
            }
          } else {
            if (actor.roomy > 0 && i >= 12 && i <= 17) {
              buf[i] = 0;               // gap
              buf[i+1] = 0;             // gap          
            }
            else {
              buf[i] = CH_FLOOR+1;	// lower-left
              buf[i+1] = CH_FLOOR+3;	// lower-right
            }
              
          }
        }
      }
      else if(line <= 25) {
        // clear buffer
        memset(buf, 0, sizeof(buf));
        // draw walls
        if(actor.roomx > 0 && line >= 10 && line <= 15){
          buf[0] = 0;	                // gap
        } else if(line % 2 == 1)
          buf[0] = CH_FLOOR;		// left side
        else
          buf[0] = CH_FLOOR+1;		// left side
        if(actor.roomx < MAX_X_ROOMS - 1 && (line >= 10 && line <= 15)){
          buf[COLS-1] = 0;	        // gap
        }
        else if(line % 2 == 1)
          buf[COLS-1] = CH_FLOOR+2;	// right side
	else
          buf[COLS-1] = CH_FLOOR+3;	// right side
          
      }
  // compute row in name buffer and address
  rowy = (ROWS-1) - (line % ROWS);
  addr = getntaddr(1, rowy);
  // copy attribute table (every 4th row)
  if ((addr & 0x60) == 0) {
    byte a;
    a = 0x00; // color
    // write entire row of attribute blocks
    memset(attrs, a, 8);
    vrambuf_put(nt2attraddr(addr), attrs, 8);
  }
  // copy line to screen buffer
  vrambuf_put(addr, buf, COLS);
}

// draw traps of current room
void draw_traps() {
  byte i;
  for (i=0; i<rooms[actor.roomx][actor.roomy].numTraps; i++) {
  oam_off = oam_meta_spr(rooms[actor.roomx][actor.roomy].xt[i], rooms[actor.roomx][actor.roomy].yt[i], oam_off, trap);
  }
}

// draw current room
void draw_room() {
  byte y;
  for (y=0; y<=25; y++) {
    draw_room_line(y);
    vrambuf_flush();
  }
}

// draw all sprites
void refresh_sprites() {
  byte i;  
  // reset sprite index to 0
  oam_off = 0;
  // draw actor
  draw_actor(&actor);
  // draw monsters of current room
  for (i=0; i<rooms[actor.roomx][actor.roomy].numMonsters; i++){
    if(rooms[actor.roomx][actor.roomy].monsters[i].life > 0)
      draw_actor(&rooms[actor.roomx][actor.roomy].monsters[i]);
  }
  // draw all bullets
  for (i=0; i<num_bullets; i++)
    draw_bullet(i);
  // draw scoreboard
  draw_scoreboard();
  // draw life of actor
  draw_life();
  // draw traps of current room
  draw_traps();  
  // hide rest
  oam_hide_rest(oam_off);
}

// returns absolute value of x
byte iabs(int x) {
  return x >= 0 ? x : -x;
}

// return index of narest monster of actor in current room
// or 99 when there are no more monsters
byte nearest_monster(){
  byte i = 0;
  byte h = 0;
  byte k = 0;
  
  for (k=0; k<rooms[actor.roomx][actor.roomy].numMonsters; k++) {
    if(!h && rooms[actor.roomx][actor.roomy].monsters[k].life > 0) {
      i = k;	
      h = 1;
    }
      
    if(iabs(actor.x - rooms[actor.roomx][actor.roomy].monsters[k].x)
      + iabs(actor.yy - rooms[actor.roomx][actor.roomy].monsters[k].yy)
      <= iabs(actor.x - rooms[actor.roomx][actor.roomy].monsters[i].x)
      + iabs(actor.yy - rooms[actor.roomx][actor.roomy].monsters[i].yy) 
       && rooms[actor.roomx][actor.roomy].monsters[k].life > 0){
      i = k;
      h = 1;
    }
  }
  if(h)
    return i;
  else
    return 99;
} 

// move an actor (player or enemies)
// joystick - game controller mask
void move_actor(struct Actor* a, byte joystick) {
  switch (a->state) {
    case STANDING:
    case WALKING:
    case WALKING2:
      if (joystick & PAD_A) {             // new bullet
        if (time_bullet == 0 && num_bullets < MAX_BULLETS) {
          if(nearest_monster() != 99) {
            bullets[num_bullets].x = actor.x;
  	    bullets[num_bullets].yy = actor.yy - 8;
            bullets[num_bullets].m = nearest_monster();
            bullets[num_bullets].state = 1;
          
  	    num_bullets++;
            time_bullet++;
          }
        }
      }  
      if (joystick & PAD_LEFT) {
        a->dir = 1;
        if(a->x > ACTOR_MIN_X){
          a->x--;
          a->state = WALKING;
        } else if(actor.roomx > 0 && (a->yy >= 10*8 && a->yy <= 15*8)){
          actor.roomx--;
          vbright = 0;
          num_bullets = 0;
          draw_room();
          a->x = ACTOR_MAX_X;
        }
        else {
          a->state = STANDING;
        }
      } else if (joystick & PAD_RIGHT) {
        a->dir = 0;
        if(a->x < ACTOR_MAX_X){
          a->x++;
          a->state = WALKING;
        } else if(actor.roomx < MAX_X_ROOMS - 1 && (a->yy >= 10*8 && a->yy <= 15*8)){
          actor.roomx++;
          vbright = 0;
          num_bullets = 0;
          draw_room();
          a->x = ACTOR_MIN_X;
        }
        else {
          a->state = STANDING;
        }                
      } else if (joystick & PAD_UP) {
        if(a->yy < ACTOR_MAX_Y){
          a->yy++;
          a->state = WALKING2;
        } else if(actor.roomy < MAX_Y_ROOMS - 1 && a->x >= 12*8 && a->x <= 17*8) {
          actor.roomy++;
          vbright = 0;
          num_bullets = 0;
          draw_room();          
          a->yy = ACTOR_MIN_Y;
        }
        else {
          a->state = STANDING;
        }              
      } else if (joystick & PAD_DOWN) {
        if(a->yy > ACTOR_MIN_Y){
          a->yy--;
          a->state = WALKING2;
        } else if(actor.roomy > 0 && a->x >= 12*8 && a->x <= 17*8) {
          actor.roomy--;
          vbright = 0;
          num_bullets = 0;
          draw_room();          
          a->yy = ACTOR_MAX_Y;
        }
        else {
          a->state = STANDING;
        }              
      } else {
        a->state = STANDING;
      }
      break;
  }
}


// read joystick 0 and move the player
void move_player() {
  byte joy = pad_poll(0);
  move_actor(&actor, joy);
}

// move monsters of current room in towards of actor
void move_monsters(){
  byte i;
  for (i=0; i<rooms[actor.roomx][actor.roomy].numMonsters; i++) {
      if(rand8() > 200 && rooms[actor.roomx][actor.roomy].monsters[i].life > 0){
        if (rooms[actor.roomx][actor.roomy].monsters[i].x > actor.x){
        move_actor(&rooms[actor.roomx][actor.roomy].monsters[i], 64); // Esquerda
        }
        else if (rooms[actor.roomx][actor.roomy].monsters[i].x < actor.x){
          move_actor(&rooms[actor.roomx][actor.roomy].monsters[i], 128); // Direita
        }
        if (rooms[actor.roomx][actor.roomy].monsters[i].yy > actor.yy){
          move_actor(&rooms[actor.roomx][actor.roomy].monsters[i], 32); // Baixo
        }
        else if (rooms[actor.roomx][actor.roomy].monsters[i].yy < actor.yy){
          move_actor(&rooms[actor.roomx][actor.roomy].monsters[i], 16); // Cima
        }
      }
  }
}

// move bullets of current room in towards of monsters
void move_bullets(){
  byte i;
  for (i=0; i<num_bullets; i++) {
    if (bullets[i].x > rooms[actor.roomx][actor.roomy].monsters[bullets[i].m].x + 4){
      bullets[i].x -= 4;
    }
    else if (bullets[i].x  + 4 < rooms[actor.roomx][actor.roomy].monsters[bullets[i].m].x){
      bullets[i].x += 4;
    }
    if (bullets[i].yy > rooms[actor.roomx][actor.roomy].monsters[bullets[i].m].yy + 4){
      bullets[i].yy -= 4;
    }
    else if (bullets[i].yy + 4 < rooms[actor.roomx][actor.roomy].monsters[bullets[i].m].yy){
      bullets[i].yy += 4;
    }
  }
}

// check to see if actor collides with monster or trap
bool check_collision(Actor* a) {
  byte i;
  if (a->state == DAMAGED) return false;
  // iterate through entire list of actors
  for (i=0; i<rooms[a->roomx][a->roomy].numMonsters; i++) {
    Actor* b = &rooms[a->roomx][a->roomy].monsters[i];
    // actors must be on same floor and within 8 pixels
    if (iabs(a->yy - b->yy) < 8 && 
        iabs(a->x - b->x) < 8 
        && rooms[actor.roomx][actor.roomy].monsters[i].life > 0) {
      return true;
    }
  }
  for (i=0; i<rooms[a->roomx][a->roomy].numTraps; i++) {
    if(iabs(a->yy - (208 - rooms[a->roomx][a->roomy].yt[i])) < 8 
       && iabs(a->x - rooms[a->roomx][a->roomy].xt[i]) < 8){
      return true;
    } 
  }
  
  return false;
}

// check to see if bullet collides with monster
bool check_collision_bullet(Bullet* a) {
  byte i;
  if (a->state == 0) return false;
  // iterate through entire list of actors
  for (i=0; i<rooms[actor.roomx][actor.roomy].numMonsters; i++) {
    Actor* b = &rooms[actor.roomx][actor.roomy].monsters[i];
    // actors must be on same floor and within 8 pixels
    if (iabs(a->yy - b->yy) < 8 && 
        iabs(a->x - b->x) < 8 
        && rooms[actor.roomx][actor.roomy].monsters[i].life > 0) {
      if(b->life > 0) {
        b->life -= 1;
        if(b->life == 0)
          score = bcd_add(score, 1);
      }
      return true;
    }
  }  
  return false;
}

// check the victory condition
bool check_win() {
  byte i;
  byte j;
  byte k;
  
  for (i=0; i<MAX_X_ROOMS; i++) {
    for (j=0; j<MAX_Y_ROOMS; j++) {
      for (k=0; k<rooms[i][j].numMonsters; k++) {
        if(rooms[i][j].monsters[k].life > 0)
          return false;
      }
    }
  }
  return true;
}

// creates the quantity and position of all traps
void createTraps(){
  byte i;
  byte j;
  byte k;
  
  for (i=0; i<MAX_X_ROOMS; i++) {
    for (j=0; j<MAX_Y_ROOMS; j++) {
      rooms[i][j].numTraps = rndint(3,4);
      for (k=0; k<rooms[i][j].numTraps; k++) {
        rooms[i][j].xt[k] = rndint(ACTOR_MIN_X + 20, ACTOR_MAX_X - 20);
  	rooms[i][j].yt[k] = rndint(ACTOR_MIN_Y + 20, ACTOR_MAX_Y - 20);
      }
    }
  }
}

// creates the quantity and position of all monsters
void createMonsters(){
  byte i;
  byte j;
  byte k;
  
  for (i=0; i<MAX_X_ROOMS; i++) {
    for (j=0; j<MAX_Y_ROOMS; j++) {
      rooms[i][j].numMonsters = rndint(2,5);
      for (k=0; k<rooms[i][j].numMonsters; k++) {
        rooms[i][j].monsters[k].state = STANDING;
  	rooms[i][j].monsters[k].name = ACTOR_ENEMY;
        rooms[i][j].monsters[k].x = rndint(ACTOR_MIN_X + 20, ACTOR_MAX_X - 20);
        rooms[i][j].monsters[k].yy = rndint(ACTOR_MIN_Y + 20, ACTOR_MAX_Y - 20);
        rooms[i][j].monsters[k].roomx = i;
        rooms[i][j].monsters[k].roomy = j;
        rooms[i][j].monsters[k].life = 3;
      }
    }
  }
}

// Game loop
void play_scene() {
     
  byte i;
  
  // create actor
  actor.state = STANDING;
  actor.name = ACTOR_PLAYER;
  actor.pal = 3;
  actor.x = ACTOR_MAX_X/2;
  actor.roomx = 0;
  actor.roomy = 0;
  actor.yy = ACTOR_Y_CENTER;
  actor.life = 3;
  
  // set scroll registers
  scroll(0, 255);
  
  // create all traps
  createTraps();
  
  // create all monsters
  createMonsters();
  
  // draw initial room
  draw_room();
  
  
  while (1) {
    // flush VRAM buffer (waits next frame)
    vrambuf_flush();
    // refresh all sprites
    refresh_sprites();
    // move player
    move_player();
    
    // move all the monsters
    move_monsters();
    
    // move all the bullets
    move_bullets();
    
    // check win condition
    if(check_win()){
      state = 3;
      break;
    }
    
    // see if the player hits any monsters or traps 
    if (check_collision(&actor) && actor.state != DAMAGED && time_collisions == 0) {
      actor.state = DAMAGED;
      sfx_play(SND_HIT,0);
      vbright = 7; // flash
      if(actor.life > 0) {
      	actor.life -= 1;
      }
      else {
        state = 2;
      	break;
      }
      time_collisions++;

    }
    else if (actor.state == DAMAGED) {
      actor.state = STANDING;
    }
    
    // see if the bullet hits any monsters
    for(i=0;i<num_bullets;i++) {
      if(check_collision_bullet(&bullets[i])) {
        sfx_play(SND_COIN,0);
        bullets[i].state = 0;
        bullets[i] = bullets[num_bullets-1];
        num_bullets--;
      }
    }
    
    // checks if the collision is on cooldown
    if(time_collisions > 50) {
    	time_collisions = 0;
    }    
    if(time_collisions != 0){
    	time_collisions++;
    }    

    // checks if the shot is on reload time
    if(time_bullet > 20) {
    	time_bullet = 0;
    }    
    if(time_bullet != 0) {
    	time_bullet++;
    }    
    
    // flash effects
    if (vbright > 4) {
      pal_bright(--vbright);
    }
    if (vbright < 4) {
      pal_bright(++vbright);
    }
  }
}

// reproduces a fade in effect
void fade_in() {
  byte vb;
  for (vb=0; vb<=4; vb++) {
    // set virtual bright value
    pal_bright(vb);
    // wait for 4/60 sec
    ppu_wait_frame();
    ppu_wait_frame();
    ppu_wait_frame();
    ppu_wait_frame();
  }
}

// displays the title screen
void show_title_screen(const byte* pal, const byte* rle) {
  // disable rendering
  ppu_off();
  // set palette, virtual bright to 0 (total black)
  pal_bg(pal);
  pal_bright(0);
  // unpack nametable into the VRAM
  vram_adr(0x2000);
  vram_unrle(rle);
  // enable rendering
  ppu_on_all();
  // fade in from black
  fade_in();
  
  while(1) {
    if(pad_poll(0) & PAD_START) break;
  }
}

/*{pal:"nes",layout:"nes"}*/
const char PALETTE[32] = { 
  0x09,			// background color

  0x11,0x30,0x27, 0x00,	
  0x1C,0x20,0x2C, 0x00,	
  0x00,0x10,0x20, 0x00,
  0x06,0x16,0x26, 0x00,

  0x16,0x35,0x24, 0x00,	
  0x00,0x37,0x25, 0x00,	
  0x0D,0x2D,0x3A, 0x00,
  0x0D,0x27,0x16,
};

// set up PPU
void setup_graphics() {
  ppu_off();
  oam_clear();
  pal_all(PALETTE);
  vram_adr(0x2000);
  vrambuf_clear();
  set_vram_update(updbuf);
  ppu_on_all();
}

// set up famitone library
void setup_sounds() {
  famitone_init(danger_streets_music_data);
  sfx_init(demo_sounds);
  nmi_set_callback(famitone_update);
}

// draw a message on the screen
void type_message(const char* charptr) {
  char ch;
  byte x,y;
  x = 5;
  // compute message y position relative to scroll
  y = ROWS*3 + 45;
  // repeat until end of string (0) is read
  while ((ch = *charptr++)) {
    while (y >= 60) y -= 60; // compute (y % 60)
    // newline character? go to start of next line
    if (ch == '\n') {
      x = 2;
      y++;
    } else {
      // put character into nametable
      vrambuf_put(getntaddr(x, y), &ch, 1);
      x++;
    }
    // typewriter sound
    sfx_play(SND_HIT,0);
    // flush buffer and wait a few frames
    vrambuf_flush();
    delay(5);
  }
}

// main program
void main() {
  setup_sounds();		// init famitone library
  while (1) {
    if(state == 0) {       // Title
      setup_graphics();
      show_title_screen(title_pal, title_rle);
      state = 1;
      score = 0;
    }
    else if(state == 1) { // Game
      setup_graphics();		// setup PPU, clear screen
      sfx_play(SND_START,0);	// play starting sound
      music_play(0);		// start the music
      play_scene();		// play the level
    }
    else if(state == 2) { // Game Over
      refresh_sprites();
      music_stop();
      type_message(GAME_OVER_TEXT);
      state = 0;
    }
    else if(state == 3) { // Victory
      refresh_sprites();
      music_stop();
      type_message(WIN_TEXT);
      state = 0;
    }
  }
}

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <SDL.h>
#include "display.h"
#include "../libs/gif/gif_load.h"

#define MAP_N 1024
#define SCALE_FACTOR 40.0

bool is_running = false;

uint32_t palette[256 * 3];

uint8_t* heightmap = NULL;   // Buffer/array to hold height values (1024*1024)
uint8_t* colormap  = NULL;   // Buffer/array to hold color values  (1024*1024)

typedef struct {
  int thrust_direction;      // direction to move back/front (-1, 0, +1)
  int turn_direction;        // direction to turn left/right (-1, 0, +1)
  int pitch_direction;       // direction to pitch player nose (-1, 0, +1) 
  int lift_direction;        // direction to move up/down (-1, 0, +1)
} player_t;

typedef struct {
  float x;                   // x position on the map
  float y;                   // y position on the map
  float height;              // height of the camera
  float horizon;             // offset of the horizon position (looking up-down)
  float zfar;                // distance of the camera looking forward
  float angle;               // camera angle (radians, clockwise)
} camera_t;

camera_t camera = {
  .x       = 512.0,
  .y       = 512.0,
  .height  = 100.0,
  .horizon = 100.0,
  .zfar    = 1000.0,
  .angle   = 1.5 * 3.141592, // (= 270 deg)
};

player_t player = {
  .thrust_direction = 0,
  .turn_direction   = 0,
  .pitch_direction  = 0,
  .lift_direction   = 0
};

void load_map(void) {
  uint8_t gif_palette[256 * 3];
  int pal_count;

  colormap = load_gif("maps/gif/map14.color.gif", &pal_count, gif_palette);
  heightmap = load_gif("maps/gif/map14.height.gif", NULL, NULL);

  for (int i = 0; i < pal_count; i++) {
    int r = gif_palette[3*i + 0];
    int g = gif_palette[3*i + 1];
    int b = gif_palette[3*i + 2];
    r = (r & 63) << 2;
    g = (g & 63) << 2;
    b = (b & 63) << 2;
    palette[i] = (b << 16) | (g << 8) | (r);
  }
}

void process_input(void) {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_QUIT: {
        is_running = false;
        break;
      }
      case SDL_KEYDOWN: {
        if (event.key.keysym.sym == SDLK_ESCAPE)
          is_running = false;
        if (event.key.keysym.sym == SDLK_UP)
          player.thrust_direction = +1;
        if (event.key.keysym.sym == SDLK_DOWN)
          player.thrust_direction = -1;
        if (event.key.keysym.sym == SDLK_RIGHT)
          player.turn_direction = +1;
        if (event.key.keysym.sym == SDLK_LEFT)
          player.turn_direction = -1;
        if (event.key.keysym.sym == SDLK_e)
          player.lift_direction = +1;
        if (event.key.keysym.sym == SDLK_d)
          player.lift_direction = -1;
        if (event.key.keysym.sym == SDLK_s)
          player.pitch_direction = +1;
        if (event.key.keysym.sym == SDLK_w)
          player.pitch_direction = -1;
        if (event.key.keysym.sym == SDLK_r)
          start_recording_gif();
        break;
      }
      case SDL_KEYUP: {
        if (event.key.keysym.sym == SDLK_UP)
          player.thrust_direction = 0;
        if (event.key.keysym.sym == SDLK_DOWN)
          player.thrust_direction = 0;
        if (event.key.keysym.sym == SDLK_RIGHT)
          player.turn_direction = 0;
        if (event.key.keysym.sym == SDLK_LEFT)
          player.turn_direction = 0;
        if (event.key.keysym.sym == SDLK_e)
          player.lift_direction = 0;
        if (event.key.keysym.sym == SDLK_d)
          player.lift_direction = 0;
        if (event.key.keysym.sym == SDLK_s)
          player.pitch_direction = 0;
        if (event.key.keysym.sym == SDLK_w)
          player.pitch_direction = 0;
        break;
      }
    }
  }
}

void update(void) {
  static int ticks_last_frame = 0;

  int time_to_wait = MILLISECS_PER_FRAME - (SDL_GetTicks() - ticks_last_frame);

  if (time_to_wait > 0 && time_to_wait <= MILLISECS_PER_FRAME) {
    SDL_Delay(time_to_wait);
  }
  
  float delta_time = (SDL_GetTicks() - ticks_last_frame) / 1000.0f;
  ticks_last_frame = SDL_GetTicks();
  
  camera.angle += player.turn_direction * 0.4f * delta_time;
  camera.x += cos(camera.angle) * player.thrust_direction * 50.0f * delta_time;
  camera.y += sin(camera.angle) * player.thrust_direction * 50.0f * delta_time;

  camera.horizon += player.pitch_direction * 40.0f * delta_time;
  camera.height += player.lift_direction * 40.0f * delta_time;
}

void draw(void) {
  clear_framebuffer(0xFFFFD085);

  float sinangle = sin(camera.angle);
  float cosangle = cos(camera.angle);

  // Left-most point of the FOV
  float plx = cosangle * camera.zfar + sinangle * camera.zfar;
  float ply = sinangle * camera.zfar - cosangle * camera.zfar;

  // Right-most point of the FOV
  float prx = cosangle * camera.zfar - sinangle * camera.zfar;
  float pry = sinangle * camera.zfar + cosangle * camera.zfar;

  // Loop 320 rays from left to right
  for (int i = 0; i < SCREEN_WIDTH; i++) {
    float deltax = (plx + (prx - plx) / SCREEN_WIDTH * i) / camera.zfar;
    float deltay = (ply + (pry - ply) / SCREEN_WIDTH * i) / camera.zfar;

    // Ray (x,y) coords
    float rx = camera.x;
    float ry = camera.y;

    // Store the tallest projected height per-ray
    float tallestheight = SCREEN_HEIGHT;

    // Loop all depth units until the zfar distance limit
    for (int z = 1; z < camera.zfar; z++) {
      rx += deltax;
      ry += deltay;

      // Find the offset that we have to go and fetch values from the heightmap
      int mapoffset = ((MAP_N * ((int)(ry) & (MAP_N - 1))) + ((int)(rx) & (MAP_N - 1)));

      // Project height values and find the height on-screen
      int projheight = (int)((camera.height - heightmap[mapoffset]) / z * SCALE_FACTOR + camera.horizon);

      // Only draw pixels if the new projected height is taller than the previous tallest height
      if (projheight < tallestheight) {
        // Draw pixels from previous max-height until the new projected height
        for (int y = projheight; y < tallestheight; y++) {
          if (y >= 0) {
            draw_pixel(i, y, palette[colormap[mapoffset]]);
          }
        }
        tallestheight = projheight;
      }
    }
  }

  render_framebuffer();
}

int main(int argc, char* args[]) {
  is_running = init_window();

  load_map();

  while (is_running) {
    process_input();
    update();
    draw();
  }

  destroy_window();

  return EXIT_SUCCESS;
}
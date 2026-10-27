#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

static void die(const char *s) {
  fprintf(stderr, "%s\n", s);
  exit(1);
}

int N = 801;
int8_t **grid;
int *gridbounds;

// you can also use this function to debug print your sandpile
static void render(char *filename) {
  int chan = 3;
  unsigned char *pixels = (unsigned char *)malloc(N * N * chan * sizeof(char));
  int stride = N * chan;
  if (!pixels)
    die("malloc failed for PNG pixel buffer");
  for (int i = 0; i < N; ++i) {
    for (int j = 0; j < N; ++j) {
      unsigned char r = 0, g = 0, b = 0;
      switch (grid[i][j]) {
      case 0: // obsidian
        r = 15;
        g = 15;
        b = 25;
        break;
      case 1: // electric cyan
        r = 0;
        g = 210;
        b = 255;
        break;
      case 2: // vibrant magenta
        r = 230;
        g = 30;
        b = 130;
        break;
      case 3: // bright gold
        r = 255;
        g = 200;
        b = 0;
        break;
      default: // white
        r = 0;
        g = 0;
        b = 0;
      }

      int pid = (i * N + j) * chan;
      pixels[pid] = r;
      pixels[pid + 1] = g;
      pixels[pid + 2] = b;
    }
  }
  if (stbi_write_png(filename, N, N, chan, pixels, stride)) {
    printf("Successfully saved sandpile identity to: %s\n", filename);
  } else {
    die("Error: stb_image_write failed to save png file\n");
  }
  free(pixels);
}

static void stabilize() {
  int changed = 0;
  do {
    changed = 0;
    for (int i = 0; i < N; ++i) {
      for (int j = gridbounds[2*i]; j <= gridbounds[2*i + 1]; ++j) {
        if (grid[i][j] < 4)
          continue;
        changed = 1; // topple it
        grid[i][j] -= 4;
        if (i > 0)
          grid[i - 1][j]++;
        if (j > 0)
          grid[i][j - 1]++;
        if (i < N - 1)
          grid[i + 1][j]++;
        if (j < N - 1)
          grid[i][j + 1]++;
      }
    }
  } while (changed);
}

void find_identity() {
  // we can precompute a "change" mask for this.
  for (int i = 0; i < N; ++i) {
    for (int j = 0; j < N; ++j) {
      grid[i][j] = 6;
    }
  }
  stabilize();
  for (int i = 0; i < N; ++i) {
    for (int j = 0; j < N; ++j) {
      grid[i][j] = 6 - grid[i][j];
    }
  }
  stabilize();
}

int main(int argc, char **argv) {
  if (argc < 2 || argc > 2) {
    fprintf(stderr, "Usage: %s N\n", argv[0]);
    return EXIT_FAILURE;
  }
  char *end;
  uintmax_t parsed = strtoumax(argv[1], &end, 10);
  if (errno || argv[1][0] < '0' || argv[1][0] > '9' || *end || !parsed ||
      parsed > INT32_MAX)
    die("grid size must be a positive int");
  N = (int)parsed;

  grid = (int8_t **)malloc(N * sizeof(int8_t *));
  for (int i = 0; i < N; ++i) {
    grid[i] = (int8_t *)malloc(N * sizeof(int8_t));
  }

  gridbounds = (int*)malloc(2 * N * sizeof(int));
  for (int i=0; i<N; i++) {
    // lower bound is always based on circle
    int jcirc_base = (int)sqrt((N/2)*(N/2) - (i - N/2)*(i - N/2));
    gridbounds[2*i] = MAX(0, N/2 - jcirc_base);

    // two possible upper bounds -- circle, and *pacman*
    int jpac = MAX(i, N - i - 1);
    gridbounds[2*i+1] = MIN(jpac, N/2 + jcirc_base);
  }

  find_identity();

  render("identity.png");
  for (int i = 0; i < N; ++i) {
    free(grid[i]);
  }
  free(grid);
  free(gridbounds);
  return EXIT_SUCCESS;
}

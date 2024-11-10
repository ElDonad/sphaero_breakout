#include <stdint.h>

#include "./physics.h"
#include "walloc.c"

#define BRICKS_PER_ROW 9
#define BRICK_ROWS 12

#define BRICK_WIDTH 1.0f / 12.0f
#define BRICK_HEIGHT 0.7f / 25.0f

#define BRICK_GAP_X 1.0f / 80.0f
#define BRICK_GAP_Y 0.7f / 70.0f

const static float MARGIN_X = (1.0f - BRICK_WIDTH * BRICKS_PER_ROW - BRICK_GAP_X * (BRICKS_PER_ROW - 1)) / 2.0f;
const static float MARGIN_Y = (0.7f - BRICK_HEIGHT * BRICK_ROWS - BRICK_GAP_Y * (BRICK_ROWS - 1)) / 2.0f;

__attribute__((import_module("env"), import_name("logWasm"))) void logWasm(char *str, size_t len);

size_t strlen(const char *str)
{
    size_t len = 0;
    while (str[len] != '\0')
    {
        len++;
    }
    return len;
}

char *int_to_string(size_t number)
{
    int comp = 1;
    int char_len = 0;
    while (comp < number)
    {
        comp *= 10;
        char_len++;
    }
    char_len = char_len == 0 ? 1 : char_len;
    char_len += 1;
    size_t i = 0;
    char *str = malloc(char_len);
    while (number > 0)
    {
        str[char_len - i - 2] = number % 10 + '0';
        number /= 10;
        i++;
    }
    str[char_len - 1] = '\0';
    return str;
}

void print(char *str)
{
    logWasm(str, strlen(str));
}

typedef struct
{
    bool destroyed;
    bool needs_erasing;

} Brick;

typedef struct
{
    size_t bricks_count;
    size_t game_count;
    size_t current_color_func;
    // Each brick gets 2 bits of data, to save space
    uint8_t brick_save[BRICK_ROWS * BRICKS_PER_ROW / 8 * 2];
} SaveState;

static struct ball *balls_memory = NULL;
static int32_t *canvas_memory = NULL;
static SaveState *state = NULL;
static uint8_t *save_data = NULL;

static double sqrt(double x)
{
    return __builtin_sqrt(x);
}

static void mymemcpy(void *dest, const void *src, size_t n)
{
    __builtin_memcpy(dest, src, n);
}

static void mymemset(void *s, int c, size_t n)
{
    __builtin_memset(s, c, n);
}

float fminf(float x, float y)
{
    return x < y ? x : y;
}

// pseudorandom number generator
static uint32_t seed = 12;
uint32_t rand()
{
    seed = seed * 1103515245 + 12345;
    return (seed / 65536) % 32768;
}

/**
 * How many bytes we should use from saveMemory()
 */
size_t saveSize(void)
{
    return sizeof(SaveState);
}

/**
 * Since some code runs on the client, and some code runs on the server, we need
 * a way to propagate our state from one side to the other. The save/load API is
 * how we do this. Take any state from the physics side, serialize it,
 * deserialize with load on the client side before render() is called
 */
void save(void)
{
    mymemcpy(save_data, state, saveSize());
}
void load(void)
{
    mymemcpy(state, save_data, saveSize());
}

Brick get_brick(SaveState *state, size_t x, size_t y)
{
    size_t brick_indice = x + y * BRICKS_PER_ROW;
    size_t brick_save_indice = brick_indice / 4;
    size_t brick_save_bit = (brick_indice % 4) * 2;

    return (Brick){state->brick_save[brick_save_indice] & (1 << brick_save_bit),
                   state->brick_save[brick_save_indice] & (1 << (brick_save_bit + 1))};
}

// Can only set false to true, TODO fix
void set_brick(SaveState *state, size_t x, size_t y, Brick brick)
{
    size_t brick_indice = x + y * BRICKS_PER_ROW;
    size_t brick_save_indice = brick_indice / 4;
    size_t brick_save_bit = (brick_indice % 4) * 2;

    state->brick_save[brick_save_indice] |= (brick.destroyed ? 1 : 0) << brick_save_bit;
    state->brick_save[brick_save_indice] |= (brick.needs_erasing ? 1 : 0) << (brick_save_bit + 1);
}

void reset_bricks(SaveState *state)
{
    mymemset(state->brick_save, 0, BRICK_ROWS * BRICKS_PER_ROW / 4);
    for (size_t i = 0; i < BRICK_ROWS; i++)
    {
        for (size_t j = 0; j < BRICKS_PER_ROW; j++)
        {
            set_brick(state, j, i, (Brick){false, false});
        }
    }
}

/**
 * Called one time in both server and client contexts. max_num_balls or
 * max_canvas_size may be 0, but in some contexts both will be set
 *
 * Use for one time initialization of chamber state, and ensure memory returned
 * by xxxMemory() calls are ready to go
 */
void init(size_t max_num_balls, size_t max_canvas_size)
{
    max_num_balls = max_num_balls == 0 ? 100 : max_num_balls;

    balls_memory = malloc(max_num_balls * sizeof(struct ball));
    canvas_memory = malloc(max_canvas_size * sizeof(int32_t));
    state = malloc(sizeof(SaveState));
    save_data = malloc(saveSize());
    state->bricks_count = BRICKS_PER_ROW * BRICK_ROWS;
    state->current_color_func = 0;
    state->game_count = 0;
    reset_bricks(state);
}

const struct vec2 NULL_VEC2 = {0};

bool apply_brick_collision(struct ball *ball, struct vec2 *brick_position, float delta, struct pos2 *final_pos)
{

    const float r = ball->r;
    struct vec2 *velocity = &ball->velocity;
    const struct pos2 *pos = &ball->pos;

    if (final_pos->x + r < brick_position->x || final_pos->x - r > brick_position->x + BRICK_WIDTH)
        return false;
    if (final_pos->y - r > brick_position->y || final_pos->y + r < brick_position->y - BRICK_HEIGHT)
        return false;

    // We have a collision !
    // we now need to find the surface of the brick that was hit
    bool collision = false;
    if (pos->y - r > brick_position->y && final_pos->y - r <= brick_position->y) // top side hit
    {
        velocity->y = -velocity->y;
        collision = true;
    }
    else if (pos->y + r < brick_position->y - BRICK_HEIGHT && final_pos->y + r >= brick_position->y - BRICK_HEIGHT) // bottom side hit
    {
        velocity->y = -velocity->y;
        collision = true;
    }
    if (pos->x + r < brick_position->x && final_pos->x + r >= brick_position->x) // left side hit
    {
        velocity->x = -velocity->x;
        collision = true;
    }
    else if (pos->x - r > brick_position->x + BRICK_WIDTH && final_pos->x - r <= brick_position->x + BRICK_WIDTH) // right side hit
    {
        velocity->x = -velocity->x;
        collision = true;
    }

    return collision;
}

uint32_t rainbow_colors(size_t x, size_t y)
{
    // rainbow gradient on 12 bricks, format 0xaabbggrr
    static const uint32_t colors[] = {
        0xff2800ff,
        0xff0048ff,
        0xff00b9ff,
        0xff00ffd2,
        0xff00ff5b,
        0xff15ff00,
        0xff86ff00,
        0xfffcff00,
        0xffff8f00,
        0xffff1d00,
        0xffff005a,
        0xffff00cc,
        0xffbf00ff,
    };
    return colors[y % BRICK_ROWS];
}

uint32_t alternating_colors(size_t x, size_t y)
{
    return (x + y) % 2 == 0 ? 0xffff0000 : 0xff0000ff;
}

uint32_t zebra_colors(size_t x, size_t y)
{
    return y % 2 == 0 ? 0xffffffff : 0xff000000;
}

uint32_t sphere_colors(size_t x, size_t y)
{
    const size_t radius = sqrt((x - BRICKS_PER_ROW / 2) * (x - BRICKS_PER_ROW / 2) + (y - BRICK_ROWS / 2) * (y - BRICK_ROWS / 2));
    if (radius < 4)
    {
        return 0xff0000ff;
    }
    else if (radius < 5)
    {
        return 0xff000000;
    }
    else
    {
        return 0xffffffff;
    }
}

uint32_t france_colors(size_t x, size_t y)
{
    if (x < BRICKS_PER_ROW / 3)
    {
        return 0xffff0000;
    }
    else if (x < 2 * BRICKS_PER_ROW / 3)
    {
        return 0xffffffff;
    }
    else
    {
        return 0xff0000ff;
    }
}

// array of color generation function pointers
uint32_t (*brick_color_functions[])(size_t, size_t) = {
    rainbow_colors,
    alternating_colors,
    zebra_colors,
    sphere_colors,
    france_colors};

#define COLOR_FUNC_COUNT (sizeof(brick_color_functions) / sizeof(brick_color_functions[0]))

/**
 * Run physics and update chamber state
 *
 * This is typically run on the server, but in some contexts is also run on the
 * client
 *
 * num balls tells us how many balls have been placed in ballsMemory() by the
 * caller (initialized in init). Delta is the amount of time passed in seconds
 * that we want to simulate in this step
 *
 * Definition of balls is provided by physics.h, or physics.zig
 */
void step(size_t num_balls, float delta)
{
    struct surface surface = {{0, 0}, {0, 0}};
    for (size_t i = 0; i < num_balls; i++)
    {
        struct ball *ball = &balls_memory[i];
        apply_gravity(ball, delta);
        struct vec2 corrected_velocity = vec2_mul(&ball->velocity, delta);
        struct pos2 final_pos = pos2_add(&ball->pos, &corrected_velocity);

        int ball_brick_x = (final_pos.x - MARGIN_X) / (BRICK_WIDTH + BRICK_GAP_X);
        ball_brick_x = ball_brick_x <= 0 ? 1 : ball_brick_x;
        ball_brick_x = ball_brick_x >= BRICKS_PER_ROW - 1 ? BRICKS_PER_ROW - 2 : ball_brick_x;

        int ball_brick_y = (0.7 - final_pos.y - MARGIN_Y) / (BRICK_HEIGHT + BRICK_GAP_Y);
        ball_brick_y = ball_brick_y <= 0 ? 1 : ball_brick_y;
        ball_brick_y = ball_brick_y >= BRICK_ROWS - 1 ? BRICK_ROWS - 2 : ball_brick_y;

        bool collided = false; // only one collision per ball per step
        for (size_t r = ball_brick_x - 1; r <= ball_brick_x + 1; r++)
        {
            for (size_t c = ball_brick_y - 1; c <= ball_brick_y + 1; c++)
            {

                Brick b = get_brick(state, r, c);

                if (b.destroyed)
                    continue;

                if (apply_brick_collision(ball, &(struct vec2){MARGIN_X + r * (BRICK_WIDTH + BRICK_GAP_X), 0.7 - (MARGIN_Y + c * (BRICK_HEIGHT + BRICK_GAP_Y))},
                                          delta, &final_pos))
                {
                    collided = true;
                    b.destroyed = true;
                    b.needs_erasing = true;
                    set_brick(state, r, c, b);
                    state->bricks_count--;
                    break;
                }
            }
            if (collided)
                break;
        }
    }
    if (state->bricks_count == 0)
    {
        reset_bricks(state);
        state->bricks_count = BRICK_ROWS * BRICKS_PER_ROW;
        state->current_color_func = (state->current_color_func + 1) % COLOR_FUNC_COUNT;
        state->game_count++;
    }
}

uint32_t get_color_for_brick(size_t x, size_t y)
{
    return brick_color_functions[state->current_color_func](x, y);
}

void render_brick(size_t x, size_t y, size_t canvas_width, size_t width, size_t height, int32_t color)
{
    for (size_t i = 0; i < height; i++)
    {
        for (size_t j = 0; j < width; j++)
        {
            canvas_memory[(y + i) * canvas_width + x + j] = color;
        }
    }
}

/**
 * Put pixels into the memory returned by canvasMemory(). Expectation is that
 * the memory has been written such that canvasMemory()[y * canvas_width + x]
 * represents a pixel at (x, y)
 *
 * Pixels are represented as 4 byte chunks of RGBA. Feel free to use a u32 with
 * 0xaabbggrr
 *
 * Note that canavs_width * canvas_height may be less than max_canvas_size, but
 * will never be greater
 *
 * canvasMemory() can be re-used between frames, so free to re-use previous
 * frame data if that is useful to you
 */

bool needs_full_render = true;
size_t game_count = 0;
void render(size_t canvas_width, size_t canvas_height)
{
    if (game_count != state->game_count)
    {
        game_count = state->game_count;
        needs_full_render = true;
    }
    if (needs_full_render)
    {
        for (size_t i = 0; i < BRICK_ROWS; i++)
        {
            for (size_t j = 0; j < BRICKS_PER_ROW; j++)
            {
                render_brick((MARGIN_X + j * (BRICK_WIDTH + BRICK_GAP_X)) * canvas_width,
                             (MARGIN_Y + i * (BRICK_HEIGHT + BRICK_GAP_Y)) * canvas_height / 0.7,
                             canvas_width,
                             BRICK_WIDTH * canvas_width, BRICK_HEIGHT * canvas_height / 0.7,
                             get_color_for_brick(j, i));
            }
        }
        needs_full_render = false;
    }
    else
    {
        for (size_t i = 0; i < BRICK_ROWS; i++)
        {
            for (size_t j = 0; j < BRICKS_PER_ROW; j++)
            {
                Brick brick = get_brick(state, j, i);
                if (brick.needs_erasing)
                {
                    brick.needs_erasing = false;
                    render_brick((MARGIN_X + j * (BRICK_WIDTH + BRICK_GAP_X)) * canvas_width,
                                 (MARGIN_Y + i * (BRICK_HEIGHT + BRICK_GAP_Y)) * canvas_height / 0.7f,
                                 canvas_width,
                                 BRICK_WIDTH * canvas_width, BRICK_HEIGHT * canvas_height / 0.7f, 0);
                }
            }
        }
    }
}

/**
 * Pointer to memory where the caller can place balls. Externally we will
 * write up to max_num_balls `struct balls`, so this needs to be large enough to
 * handle that
 */
void *ballsMemory(void)
{
    return balls_memory;
}

/**
 * Pointer to memory where the chamber will write pixels to. This needs
 * to be max_canvas_size * 4 bytes long. See render() for more info
 */
void *canvasMemory(void)
{
    return canvas_memory;
}

/**
 * Pointer to memory where we can interact with save data. Data will be
 * placed here before calling load(), and read from here after calling save()
 *
 * This memory will not be used if saveSize() returns 0
 *
 * See save()/load() for more info
 */
void *saveMemory(void)
{
    return save_data;
}

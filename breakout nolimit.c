// A simpler implementation using the physics provided API, but too slow to be used in the simulation

#include <stdint.h>

#include "./physics.h"
#include "walloc.c"

#define BRICKS_PER_ROW 9
#define BRICK_ROWS 13

static struct ball *balls_memory = NULL;
static int32_t *canvas_memory = NULL;

#define BRICK_WIDTH 1.0f / 12.0f
#define BRICK_HEIGHT 0.7f / 25.0f

#define BRICK_GAP_X 1.0f / 80.0f
#define BRICK_GAP_Y 0.7f / 70.0f

const static float MARGIN_X = (1.0f - BRICK_WIDTH * BRICKS_PER_ROW - BRICK_GAP_X * (BRICKS_PER_ROW - 1)) / 2.0f;
const static float MARGIN_Y = (0.7f - BRICK_HEIGHT * BRICK_ROWS - BRICK_GAP_Y * (BRICK_ROWS - 1)) / 2.0f;

typedef struct
{
    bool destroyed;
    bool needs_erasing;

} Brick;

static Brick bricks[BRICK_ROWS][BRICKS_PER_ROW] = {0};

static double sqrt(double x)
{
    return __builtin_sqrt(x);
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
 * Called one time in both server and client contexts. max_num_balls or
 * max_canvas_size may be 0, but in some contexts both will be set
 *
 * Use for one time initialization of chamber state, and ensure memory returned
 * by xxxMemory() calls are ready to go
 */
void init(size_t max_num_balls, size_t max_canvas_size)
{
    balls_memory = malloc(max_num_balls * sizeof(struct ball));
    canvas_memory = malloc(max_canvas_size * sizeof(int32_t));
}

void reset_bricks()
{
    for (size_t i = 0; i < BRICK_ROWS; i++)
    {
        for (size_t j = 0; j < BRICKS_PER_ROW; j++)
        {
            bricks[i][j].destroyed = false;
            bricks[i][j].needs_erasing = false;
        }
    }
}

const struct vec2 NULL_VEC2 = {0};

bool apply_vertex_collision(struct ball *ball, struct vec2 *ball_corrected_velocity, float delta, struct surface *surface)
{
    const struct vec2 normal = surface_normal(surface);
    const struct vec2 ball_position_offset = vec2_mul(&normal, -ball->r);
    const struct pos2 collision_point = pos2_add(&ball->pos, &ball_position_offset);
    struct vec2 resolution;

    const bool collision_res = surface_collision_resolution(surface, &collision_point, ball_corrected_velocity, &resolution);
    if (collision_res)
    {
        apply_ball_collision(ball, &resolution, &normal, &NULL_VEC2, delta, 1.0f);
        return true;
    }
    return false;
}

bool apply_brick_collision(struct ball *ball, struct vec2 *brick_position, float delta)
{
    struct pos2 *ball_position = &ball->pos;
    struct vec2 *ball_velocity = &ball->velocity;
    struct vec2 ball_corrected_velocity = {ball_velocity->x * delta, ball_velocity->y * delta};

    const struct vec2 brick_size = {BRICK_WIDTH, BRICK_HEIGHT};

    bool collision_res = false;

    // Check top vertex
    struct surface surface = {{brick_position->x, brick_position->y},
                              {brick_position->x + brick_size.x, brick_position->y}};

    if (apply_vertex_collision(ball, &ball_corrected_velocity, delta, &surface))
        return true;

    // Check bottom vertex
    surface = (struct surface){{brick_position->x + brick_size.x, brick_position->y - brick_size.y},
                               {brick_position->x, brick_position->y - brick_size.y}};

    if (apply_vertex_collision(ball, &ball_corrected_velocity, delta, &surface))
        return true;

    // Check left vertex
    surface = (struct surface){{brick_position->x, brick_position->y - brick_size.y},
                               {brick_position->x, brick_position->y}};
    if (apply_vertex_collision(ball, &ball_corrected_velocity, delta, &surface))
        return true;

    // Check right vertex
    surface = (struct surface){{brick_position->x + brick_size.x, brick_position->y},
                               {brick_position->x + brick_size.x, brick_position->y - brick_size.y}};
    if (apply_vertex_collision(ball, &ball_corrected_velocity, delta, &surface))
        return true;

    return false;
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

// array of function pointers
uint32_t (*brick_color_functions[])(size_t, size_t) = {
    rainbow_colors,
    alternating_colors,
    zebra_colors,
    sphere_colors,
    france_colors};

#define COLOR_FUNC_COUNT (sizeof(brick_color_functions) / sizeof(brick_color_functions[0]))

static size_t bricks_count = BRICKS_PER_ROW * BRICK_ROWS;
static bool needs_full_render = true;
static size_t current_color_func = 0;
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
    if (needs_full_render) // We can't run the game unless the bricks are rendered
    {
        for (size_t i = 0; i < num_balls; i++)
        {
            apply_gravity(&balls_memory[i], delta);
        }
        return;
    }

    struct ball previous_state;
    struct surface surface = {{0, 0}, {0, 0}};
    for (size_t i = 0; i < num_balls; i++)
    {
        struct ball *ball = &balls_memory[i];
        previous_state = *ball;
        apply_gravity(ball, delta);
        for (size_t r = 0; r < BRICKS_PER_ROW; r++)
        {
            for (size_t c = 0; c < BRICK_ROWS; c++)
            {
                Brick *brick = &bricks[c][r];
                if (brick->destroyed)
                    continue;

                if (apply_brick_collision(ball, &(struct vec2){MARGIN_X + r * (BRICK_WIDTH + BRICK_GAP_X), 0.7 - (MARGIN_Y + c * (BRICK_HEIGHT + BRICK_GAP_Y))},
                                          delta))
                {
                    brick->destroyed = true;
                    brick->needs_erasing = true;
                    bricks_count--;
                }
            }
        }
    }
    if (bricks_count == 0)
    {
        reset_bricks();
        needs_full_render = true;
        current_color_func = (current_color_func + 1) % COLOR_FUNC_COUNT;
    }
}

uint32_t get_color_for_brick(size_t x, size_t y)
{
    return brick_color_functions[current_color_func](x, y);
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

void render(size_t canvas_width, size_t canvas_height)
{
    if (needs_full_render)
    {
        bricks_count = BRICK_ROWS * BRICKS_PER_ROW;

        for (size_t i = 0; i < BRICK_ROWS; i++)
        {
            for (size_t j = 0; j < BRICKS_PER_ROW; j++)
            {
                Brick *brick = &bricks[i][j];

                // ASSERT((MARGIN_X + j * (BRICK_WIDTH + BRICK_GAP_X)) * canvas_width < canvas_width);
                // ASSERT((MARGIN_Y + i * (BRICK_HEIGHT + BRICK_GAP_Y)) * canvas_height < canvas_height);
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
                Brick *brick = &bricks[i][j];
                if (brick->needs_erasing)
                {
                    brick->needs_erasing = false;
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
 * Since some code runs on the client, and some code runs on the server, we need
 * a way to propagate our state from one side to the other. The save/load API is
 * how we do this. Take any state from the physics side, serialize it,
 * deserialize with load on the client side before render() is called
 */
void save(void) {}
void load(void) {}

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
    return NULL;
}

/**
 * How many bytes we should use from saveMemory()
 */
size_t saveSize(void)
{
    return 0;
}
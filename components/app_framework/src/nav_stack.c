#include "nav_stack.h"
#include <string.h>

void nav_stack_init(nav_stack_t *stack)
{
    memset(stack, 0, sizeof(*stack));
    stack->top = -1;
}

int nav_stack_push(nav_stack_t *stack, app_base_t *app)
{
    if (stack->top >= NAV_STACK_MAX_DEPTH - 1) return -1;
    stack->entries[++stack->top] = app;
    return 0;
}

app_base_t *nav_stack_pop(nav_stack_t *stack)
{
    if (stack->top <= 0) return NULL;
    return stack->entries[stack->top--];
}

app_base_t *nav_stack_top(const nav_stack_t *stack)
{
    if (stack->top < 0) return NULL;
    return stack->entries[stack->top];
}

app_base_t *nav_stack_below_top(const nav_stack_t *stack)
{
    if (stack->top < 1) return NULL;
    return stack->entries[stack->top - 1];
}

void nav_stack_pop_to_home(nav_stack_t *stack)
{
    while (stack->top > 0) {
        stack->top--;
    }
}

int nav_stack_depth(const nav_stack_t *stack)
{
    return stack->top + 1;
}

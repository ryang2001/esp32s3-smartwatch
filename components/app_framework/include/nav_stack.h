#ifndef NAV_STACK_H
#define NAV_STACK_H

#include "app_base.h"

#define NAV_STACK_MAX_DEPTH 8

typedef struct {
    app_base_t *entries[NAV_STACK_MAX_DEPTH];
    int         top;
} nav_stack_t;

void nav_stack_init(nav_stack_t *stack);
int nav_stack_push(nav_stack_t *stack, app_base_t *app);
app_base_t *nav_stack_pop(nav_stack_t *stack);
app_base_t *nav_stack_top(const nav_stack_t *stack);
app_base_t *nav_stack_below_top(const nav_stack_t *stack);
void nav_stack_pop_to_home(nav_stack_t *stack);
int nav_stack_depth(const nav_stack_t *stack);

#endif // NAV_STACK_H

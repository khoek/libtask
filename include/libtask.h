#ifndef __LIB_LIBTASK_H
#define __LIB_LIBTASK_H

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <stdint.h>

typedef struct libtask_loop libtask_loop_t;
typedef libtask_loop_t* libtask_loop_handle_t;

typedef enum libtask_disposition {
    LIBTASK_DISPOSITION_CONTINUE,
    LIBTASK_DISPOSITION_STOP,
} libtask_disposition_t;

typedef libtask_disposition_t (*libtask_do_once_fn_t)(void* private);

// Start a "loop" task, which repeatedly calls the `do_once()` function until instructed
// to stop with `libtask_loop_stop()`. It is up to the `do_once()` function to sleep
// or otherwise prevent triggering of the task watchdog.
esp_err_t libtask_loop_spawn(libtask_do_once_fn_t do_once, void* private, const char* name,
                             uint32_t task_stack_size, UBaseType_t task_priority, libtask_loop_handle_t* out_handle);

// Instructs the loop task to finish its current iteration and then terminates. Use
// `libtask_loop_wait_until_stopped()` to wait until the task has actually stopped.
//
// NOTE: Depending on the content of the `do_once()` function, it may be neccesary to
// send a signal to the looping task in order to wake the `do_once()` function up in
// between a call to `libtask_loop_should_stop()` and `libtask_loop_wait_until_stopped()`.
void libtask_loop_mask_should_stop(libtask_loop_handle_t handle);

// Waits for the loop task to finish its current iteration, and then terminates it. The
// function returns after `handle` has been destroyed and the corresponding `do_once()`
// will no longer be called.
//
// Returns the `private` data which was captured during the call to `libtask_loop_spawn()`.
void* libtask_loop_join(libtask_loop_handle_t handle);

#endif

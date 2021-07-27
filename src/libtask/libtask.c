#include "libtask.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <libesp.h>

static const char* TAG = "libtask";

struct libtask_loop {
    // The external handler to invoke when the loop occurs.
    libtask_do_once_fn_t do_once;
    // The private data to pass to the handler.
    void* private;

    // Used to signal that the loop task has stopped, so that
    // another task may join with it and destroy this struct
    // (and recover the captured `private` data).
    EventGroupHandle_t events;
#define LOOP_EVENT_SHOULD_STOP (1ULL << 0)
#define LOOP_EVENT_HAS_STOPPED (1ULL << 1)
};

typedef struct loop_ctrl loop_ctrl_t;
typedef struct loop_state loop_state_t;

static libtask_loop_t* alloc_loop_task(libtask_do_once_fn_t do_once, void* private) {
    libtask_loop_t* task = malloc(sizeof(libtask_loop_t));
    task->do_once = do_once;
    task->private = private;
    task->events = xEventGroupCreate();

    return task;
}

static void free_loop_task(libtask_loop_t* task) {
    vEventGroupDelete(task->events);
    free(task);
}

static void run_loop(libtask_loop_t* task_ptr) {
    libtask_loop_t task = *task_ptr;

    while (1) {
        if (xEventGroupGetBits(task.events) & LOOP_EVENT_SHOULD_STOP) {
            ESP_LOGD(TAG, "run_loop() stopping due to signal");
            return;
        }

        libtask_disposition_t disp = task.do_once(task.private);

        ESP_ERROR_CHECK(util_stack_overflow_check());

        switch (disp) {
            case LIBTASK_DISPOSITION_CONTINUE: {
                break;
            }
            case LIBTASK_DISPOSITION_STOP: {
                ESP_LOGD(TAG, "run_loop() stopping due to `do_once()` exit");
                return;
            }
            default: {
                abort();
            }
        }
    }
}

static void task_loop(void* arg) {
    libtask_loop_t* task = arg;

    run_loop(task);

    // Note: At this point we cannot free `task`, since this struct
    // contains `task->should_stop` and `task->private`, which
    // `libtask_loop_join()` needs access to.
    xEventGroupSetBits(task->events, LOOP_EVENT_HAS_STOPPED);

    ESP_LOGD(TAG, "loop task stopped");

    vTaskDelete(NULL);
}

esp_err_t libtask_loop_spawn(libtask_do_once_fn_t do_once, void* private, const char* name,
                             uint32_t task_stack_size, UBaseType_t task_priority, libtask_loop_handle_t* out_handle) {
    libtask_loop_t* task = alloc_loop_task(do_once, private);

    // Note that this task will allocate an interrupt, and therefore must happen on a well-defined core
    // (hence `xTaskCreatePinnedToCore()`), since interrupts must be deallocated on the same core they
    // were allocated on.
    BaseType_t result = xTaskCreate(&task_loop, name, task_stack_size, (void*) task, task_priority, NULL);
    if (result != pdPASS) {
        free_loop_task(task);

        ESP_LOGE(TAG, "failed to create loop task! (0x%X)", result);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "%s: %p", __func__, task);

    *out_handle = task;
    return ESP_OK;
}

void libtask_loop_mask_should_stop(libtask_loop_handle_t task) {
    ESP_LOGD(TAG, "%s: %p", __func__, task);

    xEventGroupSetBits(task->events, LOOP_EVENT_SHOULD_STOP);
}

void* libtask_loop_join(libtask_loop_handle_t task) {
    ESP_LOGD(TAG, "%s: %p", __func__, task);

    // Wait for the signal that stopping has sufficiently completed (in that
    // `handle_loop()` will no longer be called).
    while (!(xEventGroupWaitBits(task->events, LOOP_EVENT_HAS_STOPPED, false, false, portMAX_DELAY) & LOOP_EVENT_HAS_STOPPED))
        ;

    void* private = task->private;
    free_loop_task(task);
    return private;
}

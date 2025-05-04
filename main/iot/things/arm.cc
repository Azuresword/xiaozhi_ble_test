#include "iot/thing.h"
#include "board.h"
#include <driver/gpio.h>
#include <esp_log.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/mcpwm_prelude.h"
#include "driver/uart.h"

#define TAG "Arm"
#define UART_PORT UART_NUM_0
#define BUF_SIZE 1024

#define SERVO_MIN_PULSEWIDTH_US 500  // Minimum pulse width in microsecond
#define SERVO_MAX_PULSEWIDTH_US 2500  // Maximum pulse width in microsecond
#define SERVO_MIN_DEGREE        -90   // Minimum angle
#define SERVO_MAX_DEGREE        90    // Maximum angle

#define SERVO_PULSE_GPIO             12        // GPIO connects to the PWM signal line
#define SERVO_TIMEBASE_RESOLUTION_HZ 1000000  // 1MHz, 1us per tick
#define SERVO_TIMEBASE_PERIOD        20000    // 20000 ticks, 20ms

static inline uint32_t example_angle_to_compare(int angle)
{
    return (angle - SERVO_MIN_DEGREE) * (SERVO_MAX_PULSEWIDTH_US - SERVO_MIN_PULSEWIDTH_US) / (SERVO_MAX_DEGREE - SERVO_MIN_DEGREE) + SERVO_MIN_PULSEWIDTH_US;
}

namespace iot {

// 这里仅定义 Arm 的属性和方法，不包含具体的实现
class Arm : public Thing {
private:
    bool power_ = false;
    mcpwm_cmpr_handle_t comparator = NULL;
    void example_ledc_init(void) {
        ESP_LOGI(TAG, "Create timer and operator");
        mcpwm_timer_handle_t timer = NULL;
        mcpwm_timer_config_t timer_config = {
            .group_id = 0,
            .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
            .resolution_hz = SERVO_TIMEBASE_RESOLUTION_HZ,
            .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
            .period_ticks = SERVO_TIMEBASE_PERIOD,
        };
        ESP_ERROR_CHECK(mcpwm_new_timer(&timer_config, &timer));

        mcpwm_oper_handle_t oper = NULL;
        mcpwm_operator_config_t operator_config = {
            .group_id = 0, // operator must be in the same group to the timer
        };
        ESP_ERROR_CHECK(mcpwm_new_operator(&operator_config, &oper));

        ESP_LOGI(TAG, "Connect timer and operator");
        ESP_ERROR_CHECK(mcpwm_operator_connect_timer(oper, timer));

        ESP_LOGI(TAG, "Create comparator and generator from the operator");
        
        mcpwm_comparator_config_t comparator_config = {
            0, // intr_priority
            {true} // flags.update_cmp_on_tez
        };
        ESP_ERROR_CHECK(mcpwm_new_comparator(oper, &comparator_config, &comparator));

        mcpwm_gen_handle_t generator = NULL;
        mcpwm_generator_config_t generator_config = {
            .gen_gpio_num = SERVO_PULSE_GPIO,
        };
        ESP_ERROR_CHECK(mcpwm_new_generator(oper, &generator_config, &generator));

        // set the initial compare value, so that the servo will spin to the center position
        ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparator, example_angle_to_compare(0)));

        ESP_LOGI(TAG, "Set generator action on timer and compare event");
        // go high on counter empty
        ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(generator,
                                                                MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)));
        // go low on compare threshold
        ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(generator,
                                                                    MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, comparator, MCPWM_GEN_ACTION_LOW)));

        ESP_LOGI(TAG, "Enable and start timer");
        ESP_ERROR_CHECK(mcpwm_timer_enable(timer));
        ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP));
    }

public:
    Arm() : Thing("Arm", "你的手臂，可以运动"), power_(false) {
        // Set the LEDC peripheral configuration
        example_ledc_init();

        // Configure UART
        uart_config_t uart_config = {
            .baud_rate = 115200,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
        };
        ESP_ERROR_CHECK(uart_driver_install(UART_PORT, BUF_SIZE * 2, 0, 0, NULL, 0));
        ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));

        // 定义设备的属性
        properties_.AddBooleanProperty("power", "手臂是否运动", [this]() -> bool {
            return power_;
        });

        // 定义设备可以被远程执行的指令
        methods_.AddMethod("TurnOn", "手臂运用", ParameterList(), [this](const ParameterList& parameters) {
            power_ = true;
            int angle = 0;
            const char* move_msg = "MOVE\n";
            uart_write_bytes(UART_PORT, move_msg, strlen(move_msg));
            ESP_LOGI(TAG, "Angle of rotation: %d", angle);
            ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparator, example_angle_to_compare(angle)));
            //Add delay, since it takes time for servo to rotate, usually 200ms/60degree rotation under 5V power supply
            vTaskDelay(pdMS_TO_TICKS(500));

            angle = 20;
            uart_write_bytes(UART_PORT, move_msg, strlen(move_msg));
            ESP_LOGI(TAG, "Angle of rotation: %d", angle);
            ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparator, example_angle_to_compare(angle)));
            vTaskDelay(pdMS_TO_TICKS(500));

            angle = 40;
            uart_write_bytes(UART_PORT, move_msg, strlen(move_msg));
            ESP_LOGI(TAG, "Angle of rotation: %d", angle);
            ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparator, example_angle_to_compare(angle)));
            vTaskDelay(pdMS_TO_TICKS(500));

            angle = 60;
            uart_write_bytes(UART_PORT, move_msg, strlen(move_msg));
            ESP_LOGI(TAG, "Angle of rotation: %d", angle);
            ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparator, example_angle_to_compare(angle)));
            vTaskDelay(pdMS_TO_TICKS(500));

            angle = -60;
            uart_write_bytes(UART_PORT, move_msg, strlen(move_msg));
            ESP_LOGI(TAG, "Angle of rotation: %d", angle);
            ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparator, example_angle_to_compare(angle)));
        });

        methods_.AddMethod("TurnOff", "手臂返回原来的位置", ParameterList(), [this](const ParameterList& parameters) {
            power_ = false;
            // ****
        });
    }
};

} // namespace iot

DECLARE_THING(Arm);

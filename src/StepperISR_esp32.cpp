#include <driver/mcpwm.h>
#include <driver/pcnt.h>
#include <soc/mcpwm_reg.h>
#include <soc/mcpwm_struct.h>
#include <soc/pcnt_reg.h>
#include <soc/pcnt_struct.h>

#include "StepperISR.h"
#include "FastAccelStepper.h"

#if defined(ARDUINO_ARCH_ESP32)

#define TIMER_H_L_TRANSITION 200

#define TIMER_PRESCALER 0		// cannot be updated will timer is running

// Here are the global variables to interface with the interrupts
StepperQueue fas_queue[NUM_QUEUES];

//                           UUPPPPSS
#define TP_3200STEPS_PER_S 0x00138800L

void IRAM_ATTR next_command(uint8_t queueNum, uint8_t dirPin, struct queue_entry *e) {
  mcpwm_dev_t *mcpwm = queueNum < 3 ? &MCPWM0 : &MCPWM1;
  uint8_t timer = queueNum % 3;
      mcpwm->timer[timer].period.period = e->period;
	  uint8_t steps = e->steps;
      PCNT.conf_unit[timer].conf2.cnt_h_lim = steps >> 1;  // update only on zero
      if ((steps & 0x01) != 0) {
         digitalWrite(dirPin, digitalRead(dirPin) == HIGH ? LOW : HIGH);
      }
}

static void IRAM_ATTR pcnt_isr_service(void *arg) {
  StepperQueue *q = (StepperQueue *)arg;
  uint8_t rp = q->read_ptr;
  if (rp != q->next_write_ptr) {
      struct queue_entry *e = &q->entry[rp];
	  rp = (rp + 1) & QUEUE_LEN_MASK;
	  q->read_ptr = rp;
	  next_command(q->queueNum, q->dirPin, e);
  }
  else {
	  // no more commands: stop timer at period end
  mcpwm_dev_t *mcpwm = q->queueNum < 3 ? &MCPWM0 : &MCPWM1;
  uint8_t timer = q->queueNum % 3;
	  mcpwm->timer[timer].mode.start = 1; // stop at TEP
      mcpwm->channel[timer].generator[0].utez = 1;  // low at zero
      q->isRunning = false;
        if (q->autoEnablePin != 255) {
          digitalWrite(q->autoEnablePin, HIGH);
        }
  }
//  Serial.println("X");
//  Serial.println(PCNT.int_st.val);
  PCNT.int_clr.val = PCNT.int_st.val; // necessary ?
}

void StepperQueue::init(uint8_t queue_num, uint8_t step_pin) {
	Serial.println("StepperQueue init");
  _initVars();

  Serial.print("queue_num=");
  Serial.println(queue_num);
  digitalWrite(step_pin, LOW);
  pinMode(step_pin, OUTPUT);
  queueNum = queue_num;
  isRunning = false;

  Serial.print("rp=");
  Serial.print(read_ptr);
  Serial.print(" next_wp=");
  Serial.println(next_write_ptr);

  mcpwm_dev_t *mcpwm = queue_num < 3 ? &MCPWM0 : &MCPWM1;
  mcpwm_unit_t mcpwm_unit = queue_num < 3 ? MCPWM_UNIT_0 : MCPWM_UNIT_1;
  pcnt_unit_t pcnt_unit = (pcnt_unit_t)queue_num;

  pcnt_config_t cfg;
  cfg.pulse_gpio_num = step_pin;
  cfg.ctrl_gpio_num = PCNT_PIN_NOT_USED;
  cfg.lctrl_mode = PCNT_MODE_KEEP;
  cfg.hctrl_mode = PCNT_MODE_KEEP;
  cfg.pos_mode = PCNT_COUNT_INC;
  cfg.neg_mode = PCNT_COUNT_DIS;
  cfg.counter_h_lim = 1;
  cfg.counter_l_lim = 0;
  cfg.unit = pcnt_unit;
  cfg.channel = PCNT_CHANNEL_0;
  pcnt_unit_config(&cfg);

  pcnt_counter_clear(pcnt_unit);
  pcnt_counter_resume(pcnt_unit);
  pcnt_event_enable(pcnt_unit, PCNT_EVT_H_LIM);
  if (queue_num == 0) {
     // isr_service_install apparently enables the interrupt
     PCNT.int_clr.val = PCNT.int_st.val;
     pcnt_isr_service_install(ESP_INTR_FLAG_EDGE | ESP_INTR_FLAG_IRAM);
     pcnt_isr_handler_add(pcnt_unit, pcnt_isr_service, (void *)this);
  }

  Serial.print("rp=");
  Serial.print(read_ptr);
  Serial.print(" next_wp=");
  Serial.println(next_write_ptr);

  uint8_t timer = queue_num %3;
  if (timer == 0) {
	  Serial.println("INIT MODULE");
	  // Init mcwpm module for use
	  //
  switch(timer) {
	  case 0:
         mcpwm_gpio_init(mcpwm_unit, MCPWM0A, step_pin);
		 break;
	  case 1:
         mcpwm_gpio_init(mcpwm_unit, MCPWM1A, step_pin);
		 break;
	  case 2:
         mcpwm_gpio_init(mcpwm_unit, MCPWM2A, step_pin);
		 break;
  }
	  periph_module_enable(PERIPH_PWM0_MODULE);

//	  mcpwm->update_cfg.global_up_en = 1;
//	  mcpwm->update_cfg.global_force_up = 1;
//	  mcpwm->update_cfg.global_force_up = 0;
//	  REG_WRITE(MCPWM_CLK_REG(0), 0);
//	  REG_WRITE(MCPWM_CLK_CFG_REG(0), 0xffffffffL);
//	  REG_WRITE(MCPWM_CLK_REG(0), 1);
 //     mcpwm->clk_cfg.prescale = 255 - 1;    // 160 MHz/10  => 16 MHz
  //    mcpwm->clk_cfg.prescale = 255 - 1;    // 160 MHz/10  => 16 MHz
//	  mcpwm->update_cfg.global_up_en = 1;
//	  mcpwm->update_cfg.global_force_up = 1;
 //     mcpwm->clk_cfg.prescale = 255 - 1;    // 160 MHz/10  => 16 MHz
      mcpwm->clk_cfg.prescale = 10 - 1;    // 160 MHz/10  => 16 MHz

mcpwm_config_t pc;
pc.frequency = 16000000;
pc.counter_mode= MCPWM_UP_COUNTER;
pc.duty_mode = MCPWM_DUTY_MODE_0;
//mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &pc);


Serial.print("clk prescaler=");
Serial.print(mcpwm->clk_cfg.prescale);
Serial.print(" clk prescaler=");
Serial.print(REG_READ(MCPWM_CLK_CFG_REG(0)));
Serial.print("  timer prescaler=");
Serial.print(MCPWM0.timer[0].period.prescale);
Serial.print("  period=");
Serial.println(MCPWM0.timer[0].period.period);
	  mcpwm->timer_sel.operator0_sel = 0;  // timer 0 is input for operator 0
	  mcpwm->timer_sel.operator1_sel = 1;  // timer 1 is input for operator 1
	  mcpwm->timer_sel.operator2_sel = 2;  // timer 2 is input for operator 2
  }
Serial.print("clk prescaler=");
Serial.print(MCPWM0.clk_cfg.val);
Serial.print("  timer prescaler=");
Serial.print(MCPWM0.timer[0].period.prescale);
Serial.print("  period=");
Serial.println(MCPWM0.timer[0].period.period);
Serial.print("clk prescaler=");
Serial.print(MCPWM0.clk_cfg.val);
Serial.print("  timer prescaler=");
Serial.print(MCPWM0.timer[0].period.prescale);
Serial.print("  period=");
Serial.println(MCPWM0.timer[0].period.period);
  mcpwm->timer[timer].period.upmethod = 0;  // 0 = immediate update, 1 = TEZ
  mcpwm->timer[timer].period.prescale = TIMER_PRESCALER;
  mcpwm->timer[timer].period.period = 400;
  mcpwm->timer[timer].mode.val = 0;              // freeze
  mcpwm->timer[timer].sync.val = 0;              // no sync
  mcpwm->channel[timer].cmpr_cfg.a_upmethod = 0;
  mcpwm->channel[timer].cmpr_value[0].cmpr_val = TIMER_H_L_TRANSITION;
  mcpwm->channel[timer].generator[0].val = 0;
  mcpwm->channel[timer].generator[1].val = 0;
  //mcpwm->channel[timer].generator[0].utez = 2;  // high at zero
  mcpwm->channel[timer].generator[0].utez = 1;  // low at zero
  mcpwm->channel[timer].generator[0].utea = 1;  // low at compare A match
  mcpwm->channel[timer].db_cfg.val = 0;
  mcpwm->channel[timer].carrier_cfg.val = 0; // carrier disabled

  // TODO: still hardcoded channel
  mcpwm->update_cfg.op0_force_up = 1; // force update

  // at last link the output to pcnt input
  int input_sig_index;
  switch(queue_num) {
	  case 0: input_sig_index = PCNT_SIG_CH0_IN0_IDX; break;
	  case 1: input_sig_index = PCNT_SIG_CH0_IN1_IDX; break;
	  case 2: input_sig_index = PCNT_SIG_CH0_IN2_IDX; break;
	  case 3: input_sig_index = PCNT_SIG_CH0_IN3_IDX; break;
	  case 4: input_sig_index = PCNT_SIG_CH0_IN4_IDX; break;
	  case 5: input_sig_index = PCNT_SIG_CH0_IN5_IDX; break;
  }
  gpio_iomux_in(step_pin, input_sig_index);

  Serial.print("end of init rp=");
  Serial.print(read_ptr);
  Serial.print(" next_wp=");
  Serial.println(next_write_ptr);
}

// Mechanism is like this, starting from stopped motor:
//
//		init counter
//		init mcpwm
//		start mcpwm
//		-- mcpwm counter counts every L->H-transition at mcpwm.timer = 0
//		-- if counter reaches planned steps, then counter is reset and interrupt is created
//		pcnt interrupt: available time is from mcpwm.timer = 0+x to period
//			read next commmand: store period in counter shadow and steps in pcnt
//			without next command: set mcpwm to stop mode on reaching period
//

int StepperQueue::addQueueEntry(uint32_t start_delta_ticks, uint8_t steps,
                                    bool dir_high, int16_t change_ticks) {
  int32_t c_sum = 0;
  if (steps >= 128) {
    return AQE_STEPS_ERROR;
  }
  if (start_delta_ticks > ABSOLUTE_MAX_TICKS) {
    return AQE_TOO_HIGH;
  }
  if ((change_ticks != 0) && (steps > 1)) {
    c_sum = change_ticks * (steps - 1);
  }
  if (change_ticks > 0) {
    if (c_sum > 32768) {
      return AQE_CHANGE_TOO_HIGH;
    }
  } else if (change_ticks < 0) {
    if (c_sum < -32768) {
      return AQE_CHANGE_TOO_LOW;
    }
    if (start_delta_ticks + c_sum < MIN_DELTA_TICKS) {
      return AQE_CUMULATED_CHANGE_TOO_LOW;
    }
  }

  uint16_t period;
  uint8_t n_periods;
  if (start_delta_ticks > 65535) {
	  n_periods = start_delta_ticks >> 16;
	  n_periods += 1;
	  period = start_delta_ticks / n_periods;
  }
  else {
	  period = start_delta_ticks;
	  n_periods = 0;
  }

  uint8_t wp = next_write_ptr;
  uint8_t rp = read_ptr;
  struct queue_entry* e = &entry[wp];

  uint8_t next_wp = (wp + 1) & QUEUE_LEN_MASK;
  if (next_wp != rp) {
    pos_at_queue_end += dir_high ? steps : -steps;
    ticks_at_queue_end = change_ticks * (steps - 1) + start_delta_ticks;
    steps <<= 1;
    e->period = period;
	e->n_periods = n_periods;
    e->steps = (dir_high != dir_high_at_queue_end) ? steps | 0x01 : steps;
    dir_high_at_queue_end = dir_high;
#if (TEST_CREATE_QUEUE_CHECKSUM == 1)
    {
      unsigned char* x = (unsigned char*)e;
      for (uint8_t i = 0; i < sizeof(struct queue_entry); i++) {
        if (checksum & 0x80) {
          checksum <<= 1;
          checksum ^= 0xde;
        } else {
          checksum <<= 1;
        }
        checksum ^= *x++;
      }
    }
#endif
	if (isRunning) { // interrupts should be disabled
      next_write_ptr = next_wp;
	}
	else {
		// TODO steps and direction update
	  mcpwm_dev_t *mcpwm = queueNum < 3 ? &MCPWM0 : &MCPWM1;
      pcnt_unit_t pcnt_unit = (pcnt_unit_t)queueNum;
	  uint8_t timer = queueNum % 3;
	  // timer should be either a TEP or at zero
      mcpwm->channel[timer].generator[0].utez = 1;  // low at zero
      mcpwm->timer[timer].period.upmethod = 0;  // 0 = immediate update, 1 = TEZ
      mcpwm->timer[timer].period.period = 16000; // stepper start 1ms later
      PCNT.conf_unit[timer].conf2.cnt_h_lim = steps >> 1;
      pcnt_counter_clear(pcnt_unit);
      pcnt_counter_resume(pcnt_unit);
	  isRunning = true;

	  mcpwm->timer[timer].mode.val = 10;            // free run incrementing

      // wait period at zero
	  uint32_t i;
	  while (mcpwm->timer[timer].status.value >= TIMER_H_L_TRANSITION) {
		  i++;
	  }
	  //Serial.print("Loops=");
	  //Serial.println(i);

      mcpwm->channel[timer].generator[0].utez = 2;  // high at zero
      mcpwm->timer[timer].period.upmethod = 1;  // 0 = immediate update, 1 = TEZ

	  next_command(queueNum, dirPin, e);

      if (autoEnablePin != 255) {
        digitalWrite(autoEnablePin, LOW);
      }
	}

    return AQE_OK;
  }
  return AQE_FULL;
}
#endif
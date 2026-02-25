/********************************************************************
 * Modbus RTU (async)
 * 
 *******************************************************************/

#include "be_constobj.h"

extern int32_t b_mbrtu_init(bvm *vm);
extern int32_t b_mbrtu_deinit(bvm *vm);
extern int32_t b_mbrtu_start(bvm *vm);
extern int32_t b_mbrtu_stop(bvm *vm);
extern int32_t b_mbrtu_add_device(bvm *vm);
extern int32_t b_mbrtu_enable_device(bvm *vm);
extern int32_t b_mbrtu_clear_patterns(bvm *vm);
extern int32_t b_mbrtu_add_pattern(bvm *vm);
extern int32_t b_mbrtu_get_block(bvm *vm);
extern int32_t b_mbrtu_write_reg(bvm *vm);
extern int32_t b_mbrtu_write_regs(bvm *vm);

/* NEW */
extern int32_t b_mbrtu_add_datamap(bvm *vm);
extern int32_t b_mbrtu_get_data(bvm *vm);

#include "be_fixed_be_class_modbus_rtu.h"

void be_load_modbus_rtu_lib(bvm *vm) {
  be_pushntvclass(vm, &be_class_modbus_rtu);
  be_setglobal(vm, "modbus_rtu");
  be_pop(vm, 1);
}

/* @const_object_info_begin

class be_class_modbus_rtu (scope: global, name: modbus_rtu) {
  .p, var

  init, func(b_mbrtu_init)
  deinit, func(b_mbrtu_deinit)
  close, func(b_mbrtu_deinit)
  start, func(b_mbrtu_start)
  stop, func(b_mbrtu_stop)
  add_device, func(b_mbrtu_add_device)
  enable_device, func(b_mbrtu_enable_device)
  clear_patterns, func(b_mbrtu_clear_patterns)
  add_pattern, func(b_mbrtu_add_pattern)
  get_block, func(b_mbrtu_get_block)
  write_reg, func(b_mbrtu_write_reg)
  write_regs, func(b_mbrtu_write_regs)
  add_datamap, func(b_mbrtu_add_datamap)
  get_data, func(b_mbrtu_get_data)
}

@const_object_info_end */
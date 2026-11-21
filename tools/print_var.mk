# SPDX-License-Identifier: MPL-2.0
# Prints a variable of the Makefile read before this one:
#   make -C DIR -f Makefile -f print_var.mk print-NAME
print-%:
	@echo $($*)

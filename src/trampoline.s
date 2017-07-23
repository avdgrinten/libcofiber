
.global _cofiber_enter
.type _cofiber_enter, function
_cofiber_enter:
	mov %rsi, %r11

	# Save the current state.
	push %rbx
	push %rbp
	push %r12
	push %r13
	push %r14
	push %r15
	mov %rsp, %rsi

	# Call the actual coroutine.
	# Note that the stack has to be properly aligned before we do this call.
	mov %rdx, %rsp
	call *%r11
	ud2

.global _cofiber_restore
.type _cofiber_restore, function
_cofiber_restore:
	mov %rsi, %r11

	# save the current state
	push %rbx
	push %rbp
	push %r12
	push %r13
	push %r14
	push %r15
	mov %rsp, %rsi
	
	# restore the previous state
	mov %rdx, %rsp

	pop %r15
	pop %r14
	pop %r13
	pop %r12
	pop %rbp
	pop %rbx

	# call the hook function
	jmp *%r11



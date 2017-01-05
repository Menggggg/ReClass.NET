#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/user.h>
#include <experimental/filesystem>
#include <cstddef>

#include "NativeCore.hpp"

namespace fs = std::experimental::filesystem;

int ualarm(unsigned int milliseconds)
{
	struct itimerval nval = { 0 };
	nval.it_value.tv_sec = milliseconds / 1000;
	nval.it_value.tv_usec = (long int)(milliseconds % 1000) * 1000;
	struct itimerval oval;
	if (setitimer(ITIMER_REAL, &nval, &oval) < 0)
		return 0;
	else
		return oval.it_value.tv_sec;
}

pid_t waitpid_timeout(pid_t pid, int* status, int options, int timeoutInMilliseconds, bool& timedOut)
{
	struct sigaction sig = {};
	sig.sa_flags = 0;
	sig.sa_handler = [](int) {};
	sigfillset(&sig.sa_mask);
	sigaction(SIGALRM, &sig, nullptr);

	ualarm(timeoutInMilliseconds);

	auto res = waitpid(pid, status, options);
	if (res == -1 && errno == EINTR)
	{
		timedOut = true;
	}
	else
	{
		ualarm(0); // Cancel the alarm.

		timedOut = false;
	}
	return res;
}

pid_t waitpid_timeout(int* status, int timeoutInMilliseconds, bool& timedOut)
{
	return waitpid_timeout(-1, status, 0, timeoutInMilliseconds, timedOut);
}

extern "C" bool AttachDebuggerToProcess(RC_Pointer id)
{
	//TODO: Attach to all threads.

	ptrace(PTRACE_ATTACH, (pid_t)(intptr_t)id, nullptr, nullptr);
	
	waitpid(-1, nullptr, 0);

	ptrace(PTRACE_CONT, (pid_t)(intptr_t)id, nullptr, nullptr);

	return false;
}

extern "C" void DetachDebuggerFromProcess(RC_Pointer id)
{
	//TODO: Detach to all threads.

	ptrace(PTRACE_DETACH, (pid_t)(intptr_t)id, nullptr, nullptr);
}

extern "C" bool AwaitDebugEvent(DebugEvent* evt, int timeoutInMilliseconds)
{
	int status;
	bool timedOut;

	auto tid = waitpid_timeout(&status, timeoutInMilliseconds, timedOut);

	if (timedOut)
	{
		return false;
	}

	auto result = false;

	if (tid > 0)
	{
		evt->ThreadId = (RC_Pointer)(intptr_t)tid;

		siginfo_t si;
		if (ptrace(PTRACE_GETSIGINFO, tid, nullptr, &si) == 0)
		{
			if (si.si_signo == SIGTRAP)
			{
				struct user_regs_struct regs;
				if (ptrace(PTRACE_GETREGS, tid, nullptr, &regs) == 0)
				{
					DebugRegister6 dr6;
					dr6.Value = ptrace(PTRACE_PEEKUSER, tid, offsetof(struct user, u_debugreg[6]), nullptr);

					// Check if breakpoint was a hardware breakpoint.
					if (dr6.DR0)
					{
						evt->ExceptionInfo.CausedBy = HardwareBreakpointRegister::Dr0;
					}
					else if (dr6.DR1)
					{
						evt->ExceptionInfo.CausedBy = HardwareBreakpointRegister::Dr1;
					}
					else if (dr6.DR2)
					{
						evt->ExceptionInfo.CausedBy = HardwareBreakpointRegister::Dr2;
					}
					else if (dr6.DR3)
					{
						evt->ExceptionInfo.CausedBy = HardwareBreakpointRegister::Dr3;
					}
					else
					{
						evt->ExceptionInfo.CausedBy = HardwareBreakpointRegister::InvalidRegister;
					}

					// Copy registers.
					auto& reg = evt->ExceptionInfo.Registers;
#ifdef __x86_64__
					reg.Rax = (RC_Pointer)regs.rax;
					reg.Rbx = (RC_Pointer)regs.rbx;
					reg.Rcx = (RC_Pointer)regs.rcx;
					reg.Rdx = (RC_Pointer)regs.rdx;
					reg.Rdi = (RC_Pointer)regs.rdi;
					reg.Rsi = (RC_Pointer)regs.rsi;
					reg.Rsp = (RC_Pointer)regs.rsp;
					reg.Rbp = (RC_Pointer)regs.rbp;
					reg.Rip = (RC_Pointer)regs.rip;

					reg.R8 = (RC_Pointer)regs.r8;
					reg.R9 = (RC_Pointer)regs.r9;
					reg.R10 = (RC_Pointer)regs.r10;
					reg.R11 = (RC_Pointer)regs.r11;
					reg.R12 = (RC_Pointer)regs.r12;
					reg.R13 = (RC_Pointer)regs.r13;
					reg.R14 = (RC_Pointer)regs.r14;
					reg.R15 = (RC_Pointer)regs.r15;
#else
					reg.Eax = (RC_Pointer)regs.eax;
					reg.Ebx = (RC_Pointer)regs.ebx;
					reg.Ecx = (RC_Pointer)regs.ecx;
					reg.Edx = (RC_Pointer)regs.edx;
					reg.Edi = (RC_Pointer)regs.edi;
					reg.Esi = (RC_Pointer)regs.esi;
					reg.Esp = (RC_Pointer)regs.esp;
					reg.Ebp = (RC_Pointer)regs.ebp;
					reg.Eip = (RC_Pointer)regs.eip;
#endif

					result = true;
				}
			}

			if (result == false)
			{
				ptrace(PTRACE_CONT, tid, nullptr, si.si_signo);
			}
		}
	}

	return result;
}

extern "C" void HandleDebugEvent(DebugEvent* evt)
{
	auto tid = (pid_t)(intptr_t)evt->ThreadId;

	siginfo_t si;
	if (ptrace(PTRACE_GETSIGINFO, tid, 0, &si) == 0)
	{
		auto signal = 0;
		switch (evt->ContinueStatus)
		{
		case DebugContinueStatus::Handled:
			signal = 0;
			break;
		case DebugContinueStatus::NotHandled:
			signal = si.si_signo;
			break;
		}

		if (signal == SIGSTOP)
		{
			signal = 0;
		}

		ptrace(PTRACE_CONT, tid, nullptr, signal);
	}
}

extern "C" bool SetHardwareBreakpoint(RC_Pointer id, RC_Pointer address, HardwareBreakpointRegister reg, HardwareBreakpointTrigger type, HardwareBreakpointSize size, bool set)
{
	if (reg == HardwareBreakpointRegister::InvalidRegister)
	{
		return false;
	}

	intptr_t addressValue = 0;
	int accessValue = 0;
	int lengthValue = 0;

	if (set)
	{
		addressValue = (intptr_t)address;

		if (type == HardwareBreakpointTrigger::Execute)
			accessValue = 0;
		else if (type == HardwareBreakpointTrigger::Access)
			accessValue = 3;
		else if (type == HardwareBreakpointTrigger::Write)
			accessValue = 1;

		if (size == HardwareBreakpointSize::Size1)
			lengthValue = 0;
		else if (size == HardwareBreakpointSize::Size2)
			lengthValue = 1;
		else if (size == HardwareBreakpointSize::Size4)
			lengthValue = 3;
		else if (size == HardwareBreakpointSize::Size8)
			lengthValue = 2;
	}

	auto tasksPath = fs::path("/proc") / std::to_string((intptr_t)id) / "task";
	if (fs::is_directory(tasksPath))
	{
		for (auto& d : fs::directory_iterator(tasksPath))
		{
			if (fs::is_directory(d))
			{
				auto taskPath = d.path();

				auto name = taskPath.filename().string();
				if (is_number(name))
				{
					auto tid = parse_type<size_t>(name);

					// Stop the thread. TODO: Check if the thread was already paused.
					for (int i = 0; i < 10; ++i)
					{
						kill(tid, SIGSTOP);

						bool timedOut;
						waitpid_timeout(tid, nullptr, 0, 100, timedOut);
						if (!timedOut)
						{
							break;
						}
					}

					DebugRegister7 dr7;
					dr7.Value = ptrace(PTRACE_PEEKUSER, tid, offsetof(struct user, u_debugreg[7]), nullptr);

					intptr_t registerAddress;
					switch (reg)
					{
					case HardwareBreakpointRegister::Dr0:
						registerAddress = offsetof(struct user, u_debugreg[0]);
						dr7.G0 = true;
						dr7.RW0 = accessValue;
						dr7.Len0 = lengthValue;
						break;
					case HardwareBreakpointRegister::Dr1:
						registerAddress = offsetof(struct user, u_debugreg[1]);
						dr7.G1 = true;
						dr7.RW1 = accessValue;
						dr7.Len1 = lengthValue;
						break;
					case HardwareBreakpointRegister::Dr2:
						registerAddress = offsetof(struct user, u_debugreg[2]);
						dr7.G2 = true;
						dr7.RW2 = accessValue;
						dr7.Len2 = lengthValue;
						break;
					case HardwareBreakpointRegister::Dr3:
						registerAddress = offsetof(struct user, u_debugreg[3]);
						dr7.G3 = true;
						dr7.RW3 = accessValue;
						dr7.Len3 = lengthValue;
						break;
					}

					ptrace(PTRACE_POKEUSER, tid, registerAddress, addressValue);
					ptrace(PTRACE_POKEUSER, tid, offsetof(struct user, u_debugreg[7]), dr7.Value);

					ptrace(PTRACE_CONT, tid, nullptr, nullptr);
				}
			}
		}
	}

	return true;
}
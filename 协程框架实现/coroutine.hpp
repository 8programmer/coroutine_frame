#pragma once
#include<iostream>
#include<string.h>
#include<functional>
#include<stddef.h>
#include<ucontext.h>
#include<assert.h>
#include<stdint.h>
#define COROUTINE_DEAD 0//消亡
#define COROUTINE_READY 1//就绪
#define COROUTINE_SUSUPEND 2//挂起
#define COROUTINE_RUNNING 3//运行

#define DEFAULT_COROUTINE 16//调度器中管理的最大协程数
#define STACKSIZE (1024*1024)//协程栈空间大小1M


class coroutine
{
	using Task = std::function<void()>;//using Task=void(*)(void*);
	struct cell_coroutine
	{
		Task func;//指向任务函数的指针
		ptrdiff_t cap;//记录协程栈空间的最大值
		ptrdiff_t size;//记录协程栈空间的实际大小
		int status;//记录协程运行状态
		char* stack;//指向栈空间的指针
		ucontext_t ctx;//记录协程当前上下文信息
	};
	struct schedule
	{
		char stack[STACKSIZE];//调度器栈空间大小
		int cap;//记录调度器中最大协程数
		int nco;//记录调度器中实际协程数
		int running;//记录调度器中正在运行的协程id
		cell_coroutine** co;//指向协程指针的指针数组
		ucontext_t main;//记录调度器当前上下文信息
	};
	schedule* _sch;
public:
	coroutine() :_sch(nullptr) {}
	//初始化_sch
	void init()
	{
		assert(!_sch);
		_sch = new schedule{};
		if (!_sch)
			assert(0);
		_sch->cap = DEFAULT_COROUTINE;
		_sch->nco = 0;
		_sch->main = {};
		_sch->running = -1;
		memset(_sch->stack, 0, STACKSIZE);
		_sch->co = new cell_coroutine * [_sch->cap]{};
		assert(_sch->co);
	}
	//创建协程 _co_new();
	cell_coroutine* _co_new(Task func)
	{
		assert(func);//
		cell_coroutine* c = new cell_coroutine{};
		assert(c);
		c->cap = 0;
		c->size = 0;
		c->ctx = {};
		c->stack = nullptr;
		c->status = COROUTINE_READY;
		c->func = func;
		return c;
	}
	//销毁协程_co_delete();
	static void _co_delete(cell_coroutine* c)
	{
		assert(c);
		if (c->stack)
		{
			free(c->stack);
			c->stack = nullptr;
		}
		free(c);
		c = nullptr;
	}
	//将协程加入调度器管理 coroutine_new();
	int coroutine_new(Task func)
	{
		cell_coroutine* c = _co_new(func);
		if (_sch->nco >= _sch->cap)//nco=16 数组最大下标 15
		{
			int id = _sch->cap;
			cell_coroutine** temp = (cell_coroutine**)realloc(_sch->co, sizeof(cell_coroutine*) * 2 * _sch->cap);
			if (!temp)
			{
				delete[] _sch->co;
				_sch->co = nullptr;
				delete _sch;
				_sch = nullptr;
				delete c;
				c = nullptr;
				assert(temp);
			}
			_sch->co = temp;
			memset(_sch->co + _sch->cap, 0, sizeof(cell_coroutine*) * _sch->cap);
			_sch->co[id] = c;
			++_sch->nco;
			_sch->cap *= 2;
			return id;
		}
		for (int i = 0; i < _sch->cap; ++i)
		{
			int id = (_sch->nco + i) % _sch->cap;
			if (!_sch->co[id])
			{
				_sch->co[id] = c;
				++_sch->nco;
				return id;
			}
		}
		assert(0);
		return -1;
	}
	//mainfunc方法----协程对任务的调用
	static void mainfunc(uintptr_t ptr)
	{
		schedule* s = (schedule*)ptr;
		int id = s->running;
		cell_coroutine* c = s->co[id];
		assert(c);
		c->func();
		_co_delete(c);
		s->running = -1;
		--s->nco;
		s->co[id] = nullptr;
	}
	//协程的恢复运行 coroutine_resume();
	void coroutine_resume(int id)
	{
		assert(_sch->running == -1);
		assert(id >= 0 && id < _sch->cap);
		cell_coroutine* c = _sch->co[id];
		int status = c->status;
		switch (status)
		{
		case COROUTINE_READY:
		{
			getcontext(&c->ctx);
			c->ctx.uc_stack.ss_sp = _sch->stack;
			c->ctx.uc_stack.ss_size = STACKSIZE;
			c->ctx.uc_link = &_sch->main;
			c->status = COROUTINE_RUNNING;
			_sch->running = id;
			uintptr_t ptr = (uintptr_t)_sch;
			makecontext(&c->ctx, (void(*)())mainfunc, 1, ptr);
			swapcontext(&_sch->main, &c->ctx);
			break;
		}
		case COROUTINE_SUSUPEND:
		{
			memcpy(_sch->stack + STACKSIZE - c->size, c->stack, c->size);
			c->status = COROUTINE_RUNNING;
			_sch->running = id;
			swapcontext(&_sch->main, &c->ctx);
			break;
		}
		default:
			assert(0);
		}
	}
	//协程栈空间的保存 _save_stack();
	void _save_stack(cell_coroutine* c, char* top)
	{
		char dummy = 0;
		assert(top - &dummy <= STACKSIZE);
		if (c->cap < top - &dummy)
		{
			free(c->stack);
			c->cap = top - &dummy;
			c->stack = (char*)malloc(c->cap);
		}
		c->size = top - &dummy;
		memcpy(c->stack, &dummy, c->size);
	}
	//协程的让出coroutine_yield()
	void coroutine_yield()
	{
		int id = _sch->running;
		assert(id >= 0 && id < _sch->cap);
		cell_coroutine* c = _sch->co[id];
		assert((char*)&c > _sch->stack);
		_save_stack(c, _sch->stack + STACKSIZE);
		_sch->running = -1;
		c->status = COROUTINE_SUSUPEND;
		swapcontext(&c->ctx, &_sch->main);
	}
	//协程的状态 coroutine_status()
	int coroutine_status(int id)
	{
		assert(id >= 0 && id < _sch->cap);
		if (_sch->co[id])
			return _sch->co[id]->status;
		return COROUTINE_DEAD;
	}
	//获取正在运行的协程id;
	int coroutine_running()
	{
		return _sch->running;
	}
	void destroy()
	{
		if (_sch)
		{
			if (_sch->co)
			{
				for (int i = 0; i < _sch->cap; ++i)
				{
					if (_sch->co[i])
						_co_delete(_sch->co[i]);
				}
				free(_sch->co);
				_sch->co = nullptr;
			}
			free(_sch);
			_sch = nullptr;
		}
	}
	virtual~coroutine()
	{
		destroy();
		std::cout << "coroutine is destroy" << "\n";
	}

};

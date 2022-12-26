#include"coroutine.hpp"
#include<iostream>


void task1(coroutine* c, int mix)
{
	for (int i = 0; i < 5; ++i)
	{
		std::cout << "coroutine id:[" << c->coroutine_running() << "]" << i * mix << "\n";
	}
	c->coroutine_yield();
}
void task2(coroutine* c, int mix)
{
	for (int i = 0; i < 10; ++i)
	{
		std::cout << "coroutine id:[" << c->coroutine_running() << "]" << i * mix << "\n";
	}
	c->coroutine_yield();
}
void call_coroutine(coroutine* c)
{
	int co1 = c->coroutine_new(std::bind(task1, c, 10));
	int co2 = c->coroutine_new(std::bind(task2, c, 5));
	while (c->coroutine_status(co1) && c->coroutine_status(co2))
	{
		c->coroutine_resume(co1);
		c->coroutine_resume(co2);
	}
}

int main()
{
	coroutine cor;
	cor.init();
	call_coroutine(&cor);

	return 0;
}
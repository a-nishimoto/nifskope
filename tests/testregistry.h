#ifndef TESTREGISTRY_H
#define TESTREGISTRY_H

#include <QList>
#include <QObject>


//! @file testregistry.h Lets every tst_*.cpp register its test class with the single test executable

class TestRegistry
{
public:
	typedef QObject * (*Factory)();

	static QList<Factory> & factories()
	{
		static QList<Factory> list;
		return list;
	}
};

template <typename T> class TestRegistrar
{
public:
	TestRegistrar()
	{
		TestRegistry::factories().append( []() -> QObject * { return new T; } );
	}
};

//! Register a test class; use once per class, right after its definition
#define REGISTER_TEST( CLASS ) static TestRegistrar<CLASS> registrar_##CLASS;

#endif

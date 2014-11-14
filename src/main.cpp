#include "autoprinter.h"
#include <QtGui/QApplication>
#include <qtranslator.h>


int main(int argc, char *argv[])
{
	QApplication a(argc, argv);

	QTranslator translator;
	translator.load("autoprinter.qm", ".");
	a.installTranslator(&translator);

	AutoPrinter w;
	w.show();
	return a.exec();
}

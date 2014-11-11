#include "autoprinter.h"
#include <QtGui/QApplication>


int main(int argc, char *argv[])
{
	QApplication a(argc, argv);
	AutoPrinter w;
	w.show();
	return a.exec();
}

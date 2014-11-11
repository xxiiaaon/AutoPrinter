cmake_minimum_required(VERSION 2.4)
PROJECT(AutoPrint)
FIND_PACKAGE(Qt4 REQUIRED)

SET(AutoPrint_SOURCES src/main.cpp src/autoprinter.cpp)
SET(AutoPrint_HEADERS src/autoprinter.h)
SET(AutoPrint_FORMS src/autoprinter.ui)
SET(AutoPrint_RESOURCES res/autoprinter.qrc)

QT4_WRAP_CPP(autoprinter_HEADERS_MOC ${AutoPrint_HEADERS})
QT4_WRAP_UI(autoprinter_FORMS_HEADERS ${AutoPrint_FORMS})
QT4_ADD_RESOURCES(autoprinter_RESOURCES_RCC ${AutoPrint_RESOURCES})

set(QT_USE_QTGUI TRUE)
INCLUDE(${QT_USE_FILE})
ADD_DEFINITIONS(${QT_DEFINITIONS})

ADD_EXECUTABLE(AutoPrint ${AutoPrint_SOURCES} 
    ${autoprinter_HEADERS_MOC} 
    ${autoprinter_FORMS_HEADERS} 
    ${autoprinter_RESOURCES_RCC})
	
TARGET_LINK_LIBRARIES(AutoPrint ${QT_LIBRARIES})
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR})
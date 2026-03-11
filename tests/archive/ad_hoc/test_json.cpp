#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVariant>
#include <iostream>

int main() {
    QJsonObject obj;
    obj["num_int"] = 1;
    obj["num_str"] = "2";
    
    std::cout << obj["num_int"].toInt() << "\n";
    std::cout << obj["num_str"].toInt() << "\n";
    std::cout << obj["num_int"].toVariant().toString().toStdString() << "\n";
    std::cout << obj["num_str"].toVariant().toString().toStdString() << "\n";
    return 0;
}

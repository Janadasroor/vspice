#include <pybind11/embed.h>
#include <pybind11/stl.h>
#include <QPointF>
#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QVariant>
#include <QMetaType>
#include "../schematic/editor/schematic_api.h"
#include "../schematic/analysis/schematic_erc.h"

namespace py = pybind11;

// Dummy function to ensure the linker doesn't discard this object file
void flux_python_init_bindings() {}

// Helper to convert QString to std::string for pybind11
namespace pybind11 { namespace detail {
    template <> struct type_caster<QString> {
    public:
        PYBIND11_TYPE_CASTER(QString, _("str"));
        bool load(handle src, bool) {
            if (!src) return false;
            if (PyUnicode_Check(src.ptr())) {
                value = QString::fromUtf8(PyUnicode_AsUTF8(src.ptr()));
                return true;
            }
            return false;
        }
        static handle cast(QString src, return_value_policy /* policy */, handle /* parent */) {
            return PyUnicode_FromString(src.toUtf8().constData());
        }
    };

    template <> struct type_caster<QJsonObject> {
    public:
        PYBIND11_TYPE_CASTER(QJsonObject, _("dict"));
        bool load(handle src, bool) {
            if (!src) return false;
            py::module_ json = py::module_::import("json");
            py::object dumps = json.attr("dumps");
            std::string s = dumps(src).cast<std::string>();
            value = QJsonDocument::fromJson(QByteArray::fromStdString(s)).object();
            return true;
        }
        static handle cast(QJsonObject src, return_value_policy /* policy */, handle /* parent */) {
            QJsonDocument doc(src);
            std::string s = doc.toJson(QJsonDocument::Compact).toStdString();
            py::module_ json = py::module_::import("json");
            py::object loads = json.attr("loads");
            return loads(s).release();
        }
    };

    template <> struct type_caster<QVariant> {
    public:
        PYBIND11_TYPE_CASTER(QVariant, _("Any"));
        bool load(handle src, bool) {
            if (!src) return false;
            if (py::isinstance<py::str>(src)) {
                value = QString::fromStdString(src.cast<std::string>());
            } else if (py::isinstance<py::int_>(src)) {
                value = src.cast<int>();
            } else if (py::isinstance<py::float_>(src)) {
                value = src.cast<double>();
            } else if (py::isinstance<py::bool_>(src)) {
                value = src.cast<bool>();
            }
            return true;
        }
        static handle cast(QVariant src, return_value_policy /* policy */, handle /* parent */) {
            auto typeId = src.userType();
            if (typeId == QMetaType::QString) return py::str(src.toString().toStdString()).release();
            if (typeId == QMetaType::Int) return py::int_(src.toInt()).release();
            if (typeId == QMetaType::Double) return py::float_(src.toDouble()).release();
            if (typeId == QMetaType::Bool) return py::bool_(src.toBool()).release();
            return py::none().release();
        }
    };
}}

PYBIND11_EMBEDDED_MODULE(flux, m) {
    m.doc() = "VioraEDA Embedded API";

    // Bind QPointF
    py::class_<QPointF>(m, "Point")
        .def(py::init<double, double>())
        .def_property("x", &QPointF::x, &QPointF::setX)
        .def_property("y", &QPointF::y, &QPointF::setY)
        .def("__repr__", [](const QPointF &p) {
            return "Point(" + std::to_string(p.x()) + ", " + std::to_string(p.y()) + ")";
        });

    // Bind ERCViolation
    py::class_<ERCViolation>(m, "ERCViolation")
        .def_readonly("message", &ERCViolation::message)
        .def_readonly("position", &ERCViolation::position)
        .def_readonly("net_name", &ERCViolation::netName)
        .def_property_readonly("severity", [](const ERCViolation &v) {
            switch(v.severity) {
                case ERCViolation::Warning: return "Warning";
                case ERCViolation::Error: return "Error";
                case ERCViolation::Critical: return "Critical";
                default: return "Unknown";
            }
        });

    // Bind SchematicAPI
    py::class_<SchematicAPI, std::unique_ptr<SchematicAPI, py::nodelete>>(m, "SchematicAPI")
        .def(py::init<QGraphicsScene*, QUndoStack*>(), py::arg("scene") = nullptr, py::arg("undo_stack") = nullptr)
        .def("add_component", &SchematicAPI::addComponent, 
             py::arg("type"), py::arg("pos"), py::arg("reference") = "", py::arg("properties") = QJsonObject())
        .def("add_wire", &SchematicAPI::addWire, py::arg("points"))
        .def("connect", &SchematicAPI::connect, 
             py::arg("ref1"), py::arg("pin1"), py::arg("ref2"), py::arg("pin2"))
        .def("run_erc", [](SchematicAPI &self) {
             QJsonArray arr = self.runERC();
             py::list res;
             for (const auto &v : arr) {
                 QJsonObject obj = v.toObject();
                 py::dict d;
                 d["severity"] = obj["severity"].toString().toStdString();
                 d["message"] = obj["message"].toString().toStdString();
                 d["x"] = obj["x"].toDouble();
                 d["y"] = obj["y"].toDouble();
                 d["net"] = obj["net"].toString().toStdString();
                 res.append(d);
             }
             return res;
        })
        .def("annotate", &SchematicAPI::annotate, py::arg("reset_all") = false)
        .def("save", &SchematicAPI::save, py::arg("path"))
        .def("load", &SchematicAPI::load, py::arg("path"))
        .def("set_property", &SchematicAPI::setProperty, 
             py::arg("reference"), py::arg("name"), py::arg("value"));
}

#include "spice_netlist_generator.h"
#include "../items/schematic_item.h"
#include "net_manager.h"
#include "../io/netlist_generator.h"
#include "../../symbols/symbol_library.h"
#include "../../symbols/models/symbol_definition.h"
#include "../../simulator/bridge/model_library_manager.h"
#include "../items/schematic_spice_directive_item.h"
#include "../items/tuning_slider_symbol_item.h"
#include <QGraphicsScene>
#include <QSet>
#include <QMap>
#include <QDebug>
#include <QDateTime>
#include <QRegularExpression>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <QCryptographicHash>
#include <cmath>
#include <limits>
#include "config_manager.h"
#include "../../simulator/core/sim_value_parser.h"
#include "../../simulator/mixedmode/NetlistManager.h"

using Flux::Model::SymbolDefinition;

namespace {
struct UserSpiceContentSummary {
    QSet<QString> declaredModelFiles;
    QSet<QString> declaredModelNames;
    QSet<QString> declaredElementRefs;
    QSet<QString> drivenRailNets;
    QStringList warnings;
    bool hasExplicitAnalysisCard = false;
    bool hasElementCards = false;
    bool hasLtspiceStartup = false;
    bool hasExplicitSaveDirective = false;
    bool hasNetDirective = false;
};

struct PassiveCompanionParams {
    QString baseValue;
    QString rser;
    QString rpar;
    QString cpar;
    QString ic;
};

bool isLikelyLogicInputPinName(const QString& rawPinName) {
    QString pin = rawPinName.trimmed().toLower();
    pin.remove(QRegularExpression("(\\[[0-9]+\\]|<[0-9]+>|[0-9]+)$"));
    if (pin.isEmpty()) return false;
    if (pin.contains("out") || pin == "q" || pin == "qn" || pin == "qbar" || pin == "y" || pin == "z" || pin == "f")
        return false;
    return pin.contains("in") || pin == "a" || pin == "b" || pin == "c" || pin == "d" ||
           pin == "e" || pin == "clk" || pin == "clock" || pin == "en" || pin == "enable" ||
           pin == "rst" || pin == "reset" || pin == "set" || pin == "s" || pin == "r" ||
           pin == "j" || pin == "k" || pin == "t";
}

PassiveCompanionParams parsePassiveCompanionParams(const QString& rawValue) {
    PassiveCompanionParams out;
    const QString text = rawValue.trimmed();
    if (text.isEmpty()) return out;

    const QStringList tokens = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (!tokens.isEmpty()) out.baseValue = tokens.first().trimmed();

    auto extract = [&](const char* key, QString* target) {
        const QRegularExpression re(
            QString("\\b%1\\s*=\\s*([^\\s]+)").arg(QRegularExpression::escape(QString::fromLatin1(key))),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = re.match(text);
        if (match.hasMatch()) *target = match.captured(1).trimmed();
    };

    extract("Rser", &out.rser);
    extract("Rpar", &out.rpar);
    extract("Cpar", &out.cpar);
    extract("ic", &out.ic);
    return out;
}

bool isLikelyLogicOutputPinName(const QString& rawPinName) {
    QString pin = rawPinName.trimmed().toLower();
    pin.remove(QRegularExpression("(\\[[0-9]+\\]|<[0-9]+>|[0-9]+)$"));
    if (pin.isEmpty()) return false;
    return pin.contains("out") || pin == "q" || pin == "qn" || pin == "qbar" ||
           pin == "y" || pin == "z" || pin == "f" || pin == "sum" || pin == "carry";
}

QString sanitizeMixedModeToken(const QString& raw) {
    QString out = raw.trimmed();
    out.replace(QRegularExpression("[^A-Za-z0-9_]+"), "_");
    out.remove(QRegularExpression("^_+"));
    out.remove(QRegularExpression("_+$"));
    return out.isEmpty() ? QStringLiteral("MM") : out;
}

QString pinNameForHeuristics(const SymbolDefinition* sym, const QString& pinIdentifier) {
    if (!sym) return pinIdentifier;
    const auto* pin = sym->pinPrimitive(pinIdentifier);
    if (!pin) return pinIdentifier;
    const QString name = pin->data.value("name").toString().trimmed();
    return name.isEmpty() ? pinIdentifier : name;
}

struct VectorPinInfo {
    QString groupName;
    int index = -1;
    bool valid = false;
};

VectorPinInfo vectorPinInfo(const SymbolDefinition* sym, const QString& pinIdentifier, const QString& fallbackName) {
    if (sym) {
        if (const auto* pin = sym->pinPrimitive(pinIdentifier)) {
            const QString metaGroup = pin->data.value("signalVectorGroup").toString().trimmed();
            bool okIndex = false;
            const int metaIndex = pin->data.value("signalVectorIndex").toString().toInt(&okIndex);
            if (!metaGroup.isEmpty() && okIndex) {
                return {metaGroup, metaIndex, true};
            }
        }
    }

    const QString pinName = fallbackName.trimmed();
    const QRegularExpression patterns[] = {
        QRegularExpression("^(.+)\\[(\\d+)\\]$"),
        QRegularExpression("^(.+)<(\\d+)>$"),
        QRegularExpression("^([A-Za-z_]+)(\\d+)$")
    };

    for (const QRegularExpression& re : patterns) {
        const QRegularExpressionMatch match = re.match(pinName);
        if (!match.hasMatch()) continue;

        bool okIndex = false;
        const int index = match.captured(2).toInt(&okIndex);
        if (!okIndex) continue;

        QString group = match.captured(1).trimmed();
        if (group.isEmpty()) continue;
        return {group, index, true};
    }

    return {};
}

struct XspicePinAssignment {
    QString pinIdentifier;
    QString pinName;
    QString netName;
    bool isInput = true;
    int order = 0;
    VectorPinInfo vector;
};

QStringList buildXspiceNodeTokens(const QList<XspicePinAssignment>& assignments,
                                  bool collapseScalarInputsToVector = false) {
    struct GroupedVector {
        bool isInput = true;
        int firstOrder = 0;
        QList<XspicePinAssignment> members;
    };

    QMap<QString, GroupedVector> groupedVectors;
    QMap<QString, int> groupOrder;
    QList<XspicePinAssignment> scalars;
    int nextGroupOrder = 0;

    for (const XspicePinAssignment& assignment : assignments) {
        if (assignment.vector.valid) {
            const QString key = QString("%1|%2").arg(assignment.isInput ? "I" : "O", assignment.vector.groupName);
            if (!groupedVectors.contains(key)) {
                GroupedVector group;
                group.isInput = assignment.isInput;
                group.firstOrder = assignment.order;
                groupedVectors.insert(key, group);
                groupOrder.insert(key, nextGroupOrder++);
            }
            groupedVectors[key].members.append(assignment);
            continue;
        }
        scalars.append(assignment);
    }

    std::sort(scalars.begin(), scalars.end(), [](const XspicePinAssignment& a, const XspicePinAssignment& b) {
        return a.order < b.order;
    });

    QList<QPair<int, QString>> orderedGroups;
    for (auto it = groupedVectors.constBegin(); it != groupedVectors.constEnd(); ++it) {
        orderedGroups.append(qMakePair(it.value().firstOrder, it.key()));
    }
    std::sort(orderedGroups.begin(), orderedGroups.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    QStringList inputs;
    QStringList outputs;

    auto appendScalar = [&](const XspicePinAssignment& assignment) {
        (assignment.isInput ? inputs : outputs).append(assignment.netName);
    };

    auto appendVector = [&](const GroupedVector& group) {
        QList<XspicePinAssignment> members = group.members;
        std::sort(members.begin(), members.end(), [](const XspicePinAssignment& a, const XspicePinAssignment& b) {
            if (a.vector.index != b.vector.index) return a.vector.index < b.vector.index;
            return a.order < b.order;
        });

        QStringList nets;
        for (const XspicePinAssignment& member : members) {
            nets.append(member.netName);
        }

        if (nets.size() == 1) {
            if (group.isInput) inputs.append(nets.first());
            else outputs.append(nets.first());
            return;
        }

        const QString token = "[" + nets.join(" ") + "]";
        if (group.isInput) inputs.append(token);
        else outputs.append(token);
    };

    int scalarIndex = 0;
    for (const auto& ordered : orderedGroups) {
        while (scalarIndex < scalars.size() && scalars[scalarIndex].order < ordered.first) {
            appendScalar(scalars[scalarIndex++]);
        }
        appendVector(groupedVectors.value(ordered.second));
    }
    while (scalarIndex < scalars.size()) {
        appendScalar(scalars[scalarIndex++]);
    }

    if (collapseScalarInputsToVector && inputs.size() > 1) {
        QStringList tokens;
        tokens.append("[" + inputs.join(" ") + "]");
        tokens.append(outputs);
        return tokens;
    }

    return inputs + outputs;
}

QString mixedModeAdcBridgeLine(const QString& ref, const QString& pinName, const QString& analogNet, const QString& digitalNet) {
    return QString("XMM_ADC_%1_%2 %3 %4 __viospice_adc_wrap")
        .arg(sanitizeMixedModeToken(ref), sanitizeMixedModeToken(pinName), analogNet, digitalNet);
}

QString mixedModeDacBridgeLine(const QString& ref, const QString& pinName, const QString& digitalNet, const QString& analogNet) {
    return QString("XMM_DAC_%1_%2 %3 %4 __viospice_dac_wrap")
        .arg(sanitizeMixedModeToken(ref), sanitizeMixedModeToken(pinName), digitalNet, analogNet);
}

QString normalizeXspiceModelAlias(const QString& rawToken, const QString& typeName) {
    const QString token = rawToken.trimmed().toLower();
    const QString type = typeName.trimmed().toLower();

    auto matches = [&](std::initializer_list<const char*> vals) {
        for (const char* v : vals) {
            const QString q = QString::fromLatin1(v);
            if (token == q || type == q) return true;
        }
        return false;
    };

    if (matches({"d_and", "and", "gate_and", "and_gate"})) return "d_and";
    if (matches({"d_nand", "nand", "gate_nand", "nand_gate"})) return "d_nand";
    if (matches({"d_or", "or", "gate_or", "or_gate"})) return "d_or";
    if (matches({"d_nor", "nor", "gate_nor", "nor_gate"})) return "d_nor";
    if (matches({"d_xor", "xor", "gate_xor", "xor_gate"})) return "d_xor";
    if (matches({"d_xnor", "xnor", "gate_xnor", "xnor_gate"})) return "d_xnor";
    if (matches({"d_buffer", "buffer", "buf", "gate_buf", "gate_buffer"})) return "d_buffer";
    if (matches({"d_inverter", "inverter", "inv", "not", "gate_not", "not_gate"})) return "d_inverter";
    if (matches({"d_dff", "dff", "flipflop", "flip_flop", "d_flipflop", "d_flip_flop", "dff_gate", "dflop"})) return "d_dff";
    if (matches({"d_jkff", "jkff", "jk_flipflop", "jk_flip_flop", "jkflop"})) return "d_jkff";
    if (matches({"d_tff", "tff", "toggle_flipflop", "toggle_flip_flop", "toggleflop", "t_flipflop", "t_flip_flop"})) return "d_tff";
    if (matches({"d_srff", "srff", "sr_flipflop", "sr_flip_flop", "set_reset_flipflop", "set_reset_flip_flop"})) return "d_srff";
    if (matches({"d_dlatch", "dlatch", "d_latch", "d_type_latch"})) return "d_dlatch";
    if (matches({"d_srlatch", "srlatch", "sr_latch", "set_reset_latch"})) return "d_srlatch";
    if (matches({"d_tristate", "tristate", "tri_state"})) return "d_tristate";
    if (matches({"d_ram", "ram", "memory", "digital_ram"})) return "d_ram";
    if (type.contains("smartsignal") || type.contains("smart signal") || token.contains("fluxscript")) return "flux_script";

    if (token.startsWith("d_")) return token;
    return QString();
}

bool isXspiceLogicComponent(const QString& rawToken, const QString& typeName, const QString& reference) {
    if (reference.startsWith("A", Qt::CaseInsensitive)) {
        return true;
    }
    return !normalizeXspiceModelAlias(rawToken, typeName).isEmpty();
}

QString defaultXspiceModelLine(const QString& ref, const QString& codeModel) {
    const QString modelName = QString("__XSPICE_%1").arg(sanitizeMixedModeToken(ref));

    if (codeModel == "d_and" || codeModel == "d_nand" || codeModel == "d_or" ||
        codeModel == "d_nor" || codeModel == "d_xor" || codeModel == "d_xnor" ||
        codeModel == "d_buffer" || codeModel == "d_inverter" || codeModel == "d_tristate") {
        return QString(".model %1 %2(rise_delay=1n fall_delay=1n input_load=1p)")
            .arg(modelName, codeModel);
    }

    if (codeModel == "d_dff" || codeModel == "d_jkff" || codeModel == "d_tff" ||
        codeModel == "d_srff" || codeModel == "d_dlatch" || codeModel == "d_srlatch") {
        return QString(".model %1 %2(rise_delay=1n fall_delay=1n)")
            .arg(modelName, codeModel);
    }

    if (codeModel == "d_ram") {
        return QString(".model %1 d_ram(select_value=1 ic=2 read_delay=80n)")
            .arg(modelName);
    }

    if (codeModel == "flux_script") {
        return QString(".model %1 flux_script(script_id=\"%2\")").arg(modelName, ref);
    }

    return QString(".model %1 %2").arg(modelName, codeModel);
}

QString nativeLogicKeywordForCodeModel(const QString& codeModel) {
    if (codeModel == "d_and") return "AND";
    if (codeModel == "d_nand") return "NAND";
    if (codeModel == "d_or") return "OR";
    if (codeModel == "d_nor") return "NOR";
    if (codeModel == "d_xor") return "XOR";
    if (codeModel == "d_xnor") return "XNOR";
    if (codeModel == "d_buffer") return "BUF";
    if (codeModel == "d_inverter") return "NOT";
    if (codeModel == "d_dff") return "DFF";
    if (codeModel == "d_jkff") return "JKFF";
    if (codeModel == "d_tff") return "TFF";
    if (codeModel == "d_srff") return "SRFF";
    if (codeModel == "d_dlatch") return "DLATCH";
    if (codeModel == "d_srlatch") return "SRLATCH";
    if (codeModel == "d_tristate") return "TRISTATE";
    if (codeModel == "d_ram") return "RAM";
    return QString();
}

bool usesNativeLogicADevice(const QString& codeModel) {
    return !nativeLogicKeywordForCodeModel(codeModel).isEmpty();
}

bool xspiceModelUsesCollapsedInputVector(const QString& codeModel) {
    return codeModel == "d_and" || codeModel == "d_nand" ||
           codeModel == "d_or" || codeModel == "d_nor" ||
           codeModel == "d_xor" || codeModel == "d_xnor" ||
           codeModel == "flux_script";
}

NetlistManager::PinDirection pinDirectionFromMetadata(const SymbolDefinition* sym, const QString& pinName, bool* hasExplicitMetadata = nullptr) {
    if (hasExplicitMetadata) *hasExplicitMetadata = false;
    if (!sym) return NetlistManager::PinDirection::INPUT;

    const QString direction = sym->pinSignalDirection(pinName);
    if (direction == "input") {
        if (hasExplicitMetadata) *hasExplicitMetadata = true;
        return NetlistManager::PinDirection::INPUT;
    }
    if (direction == "output") {
        if (hasExplicitMetadata) *hasExplicitMetadata = true;
        return NetlistManager::PinDirection::OUTPUT;
    }
    if (direction == "bidirectional") {
        if (hasExplicitMetadata) *hasExplicitMetadata = true;
        return NetlistManager::PinDirection::BIDIRECTIONAL;
    }

    return NetlistManager::PinDirection::INPUT;
}

NodeType pinDomainFromMetadata(const SymbolDefinition* sym, const QString& pinName, bool* hasExplicitMetadata = nullptr) {
    if (hasExplicitMetadata) *hasExplicitMetadata = false;
    if (!sym) return NodeType::ANALOG;

    const QString domain = sym->pinSignalDomain(pinName);
    if (domain == "digital" || domain == "digital_event" || domain == "event") {
        if (hasExplicitMetadata) *hasExplicitMetadata = true;
        return NodeType::DIGITAL_EVENT;
    }
    if (domain == "analog") {
        if (hasExplicitMetadata) *hasExplicitMetadata = true;
        return NodeType::ANALOG;
    }

    return NodeType::ANALOG;
}

QStringList splitTopLevelSpiceArgs(const QString& text) {
    QStringList args;
    QString current;
    int parenDepth = 0;
    int braceDepth = 0;

    for (QChar ch : text) {
        if (ch == ',' && parenDepth == 0 && braceDepth == 0) {
            args.append(current.trimmed());
            current.clear();
            continue;
        }
        if (ch == '(') ++parenDepth;
        else if (ch == ')' && parenDepth > 0) --parenDepth;
        else if (ch == '{') ++braceDepth;
        else if (ch == '}' && braceDepth > 0) --braceDepth;
        current += ch;
    }
    args.append(current.trimmed());
    return args;
}

int findMatchingParen(const QString& text, int openIndex) {
    if (openIndex < 0 || openIndex >= text.size() || text.at(openIndex) != '(') return -1;
    int depth = 0;
    for (int i = openIndex; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == '(') ++depth;
        else if (ch == ')') {
            --depth;
            if (depth == 0) return i;
        }
    }
    return -1;
}

QString inlinePwlFileIfNeeded(const QString& value, const QString& projectDir, QStringList* warnings = nullptr);
bool rewriteLtspiceCurrentSourceSpecial(const QString& ref, const QString& nplus, const QString& nminus,
                                        const QString& value, const QString& projectDir,
                                        QString* replacement, QStringList* warnings = nullptr);

bool convertLtspiceConditionToStepExpr(const QString& condition, QString* stepExpr) {
    if (!stepExpr) return false;

    int parenDepth = 0;
    int braceDepth = 0;
    int opPos = -1;
    QString op;
    for (int i = 0; i < condition.size(); ++i) {
        const QChar ch = condition.at(i);
        if (ch == '(') ++parenDepth;
        else if (ch == ')' && parenDepth > 0) --parenDepth;
        else if (ch == '{') ++braceDepth;
        else if (ch == '}' && braceDepth > 0) --braceDepth;
        if (parenDepth != 0 || braceDepth != 0) continue;
        if (i + 1 < condition.size()) {
            const QString two = condition.mid(i, 2);
            if (two == ">=" || two == "<=") {
                opPos = i;
                op = two;
                break;
            }
        }
        if (ch == '>' || ch == '<') {
            opPos = i;
            op = ch;
            break;
        }
    }

    if (opPos >= 0) {
        const QString lhs = condition.left(opPos).trimmed();
        const QString rhs = condition.mid(opPos + op.size()).trimmed();
        if (lhs.isEmpty() || rhs.isEmpty()) return false;
        if (op == ">" || op == ">=") *stepExpr = QString("u((%1)-(%2))").arg(lhs, rhs);
        else if (op == "<" || op == "<=") *stepExpr = QString("u((%1)-(%2))").arg(rhs, lhs);
        else return false;
        return true;
    }

    *stepExpr = QString("u((%1)-(0.5))").arg(condition.trimmed());
    return !condition.trimmed().isEmpty();
}

void updateSubcktDepthForLine(const QString& line, int& subcktDepth) {
    const QString trimmed = line.trimmed();
    if (!trimmed.startsWith('.')) return;
    const QString card = trimmed.section(QRegularExpression("\\s+"), 0, 0).trimmed().toLower();
    if (card == ".subckt") {
        ++subcktDepth;
    } else if (card == ".ends" && subcktDepth > 0) {
        --subcktDepth;
    }
}

QString rewriteLtspiceBehavioralIf(const QString& line, QStringList* warnings = nullptr) {
    QString out = line;

    auto findTopLevelComparison = [](const QString& text, int* opPos, QString* op) {
        int parenDepth = 0;
        int braceDepth = 0;
        for (int i = 0; i < text.size(); ++i) {
            const QChar ch = text.at(i);
            if (ch == '(') ++parenDepth;
            else if (ch == ')' && parenDepth > 0) --parenDepth;
            else if (ch == '{') ++braceDepth;
            else if (ch == '}' && braceDepth > 0) --braceDepth;
            if (parenDepth != 0 || braceDepth != 0) continue;
            if (i + 1 < text.size()) {
                const QString two = text.mid(i, 2);
                if (two == ">=" || two == "<=") {
                    *opPos = i;
                    *op = two;
                    return true;
                }
            }
            if (ch == '>' || ch == '<') {
                *opPos = i;
                *op = ch;
                return true;
            }
        }
        return false;
    };

    bool changed = false;
    bool rewroteNonZeroFalseBranch = false;
    while (true) {
        const int ifPos = out.indexOf(QRegularExpression("\\bif\\s*\\(", QRegularExpression::CaseInsensitiveOption));
        if (ifPos < 0) break;
        const int openPos = out.indexOf('(', ifPos);
        const int closePos = findMatchingParen(out, openPos);
        if (openPos < 0 || closePos < 0) break;

        const QString inside = out.mid(openPos + 1, closePos - openPos - 1);
        const QStringList args = splitTopLevelSpiceArgs(inside);
        if (args.size() != 3) break;

        const QString condExpr = args.at(0).trimmed();
        const QString trueExpr = args.at(1).trimmed();
        const QString falseExpr = args.at(2).trimmed();

        int opPos = -1;
        QString op;
        if (!findTopLevelComparison(condExpr, &opPos, &op)) break;

        const QString lhs = condExpr.left(opPos).trimmed();
        const QString rhs = condExpr.mid(opPos + op.size()).trimmed();
        if (lhs.isEmpty() || rhs.isEmpty()) break;

        QString stepExpr;
        if (op == ">" || op == ">=") stepExpr = QString("u((%1)-(%2))").arg(lhs, rhs);
        else if (op == "<" || op == "<=") stepExpr = QString("u((%1)-(%2))").arg(rhs, lhs);
        else break;

        const bool falseIsZero = falseExpr == "0" || falseExpr == "0.0";
        QString replacement;
        if (falseIsZero) {
            replacement = QString("((%1)*(%2))").arg(trueExpr, stepExpr);
        } else {
            replacement = QString("((%1)*(%2) + (%3)*(1-(%2)))").arg(trueExpr, stepExpr, falseExpr);
            rewroteNonZeroFalseBranch = true;
        }

        out.replace(ifPos, closePos - ifPos + 1, replacement);
        changed = true;
    }

    if (changed && warnings) {
        warnings->append(QString("Rewrote LTspice-style if(...) to ngspice-safe expression in: %1").arg(line.trimmed()));
        if (rewroteNonZeroFalseBranch) {
            warnings->append(QString("Rewrote LTspice-style if(..., true, false) into weighted u(...) form in: %1").arg(line.trimmed()));
        }
    }
    return out;
}

QString rewriteLtspiceVoltageSourceExtras(const QString& line, QStringList* warnings = nullptr) {
    QString out = line;

    static const QRegularExpression voltageSourceExtrasRe(
        "^\\s*(V[^\\s]*)\\s+(\\S+)\\s+(\\S+)\\s+(.+?)\\s+(.*)$",
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch sourceMatch = voltageSourceExtrasRe.match(out);
    if (!sourceMatch.hasMatch()) return out;

    const QString ref = sourceMatch.captured(1).trimmed();
    const QString nodePlus = sourceMatch.captured(2).trimmed();
    const QString nodeMinus = sourceMatch.captured(3).trimmed();
    const QString value = sourceMatch.captured(4).trimmed();
    QString extras = sourceMatch.captured(5).trimmed();

    static const QRegularExpression rserRe("\\bRser\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression cparRe("\\bCpar\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch rserMatch = rserRe.match(extras);
    const QRegularExpressionMatch cparMatch = cparRe.match(extras);

    if (!rserMatch.hasMatch() && !cparMatch.hasMatch()) return out;

    const QString rser = rserMatch.hasMatch() ? rserMatch.captured(1).trimmed() : QString();
    const QString cpar = cparMatch.hasMatch() ? cparMatch.captured(1).trimmed() : QString();

    extras.remove(rserRe);
    extras.remove(cparRe);
    extras = extras.simplified();

    const QString sourcePlusNode = rser.isEmpty() ? nodePlus : QString("%1__rser").arg(ref);
    QStringList rewrittenLines;

    QString sourceLine = QString("%1 %2 %3 %4").arg(ref, sourcePlusNode, nodeMinus, value);
    if (!extras.isEmpty()) sourceLine += " " + extras;
    rewrittenLines << sourceLine;

    if (!rser.isEmpty()) {
        rewrittenLines << QString("R__RSER_%1 %2 %3 %4").arg(ref, nodePlus, sourcePlusNode, rser);
    }
    if (!cpar.isEmpty()) {
        rewrittenLines << QString("C__CPAR_%1 %2 %3 %4").arg(ref, nodePlus, nodeMinus, cpar);
    }

    out = rewrittenLines.join("\n");
    if (warnings) {
        if (!rser.isEmpty() && !cpar.isEmpty()) {
            warnings->append(QString("Expanded LTspice voltage source Rser=/Cpar= on %1 into explicit series resistor and shunt capacitor for ngspice.").arg(ref));
        } else if (!rser.isEmpty()) {
            warnings->append(QString("Expanded LTspice voltage source Rser= on %1 into explicit series resistor for ngspice.").arg(ref));
        } else {
            warnings->append(QString("Expanded LTspice voltage source Cpar= on %1 into explicit shunt capacitor for ngspice.").arg(ref));
        }
    }
    return out;
}

QString rewriteLtspiceTriggeredPulseSource(const QString& line, QStringList* warnings = nullptr) {
    static const QRegularExpression sourceRe(
        "^\\s*(V\\S*)\\s+(\\S+)\\s+(\\S+)\\s+(.+)$",
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = sourceRe.match(line);
    if (!match.hasMatch()) return line;

    const QString ref = match.captured(1).trimmed();
    const QString nplus = match.captured(2).trimmed();
    const QString nminus = match.captured(3).trimmed();
    const QString rest = match.captured(4).trimmed();
    const int pulsePos = rest.indexOf(QRegularExpression("^PULSE\\s*\\(", QRegularExpression::CaseInsensitiveOption));
    if (pulsePos != 0) return line;
    const int openPos = rest.indexOf('(');
    const int closePos = findMatchingParen(rest, openPos);
    if (openPos < 0 || closePos < 0) return line;

    const QString pulseExpr = rest.left(closePos + 1).trimmed();
    QString tail = rest.mid(closePos + 1).trimmed();

    static const QRegularExpression triggerRe("\\bTrigger\\s*=\\s*(.+?)(?=\\s+tripd[vt]\\s*=|$)", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch triggerMatch = triggerRe.match(tail);
    if (!triggerMatch.hasMatch()) return line;

    const QString triggerExpr = triggerMatch.captured(1).trimmed();
    QString stepExpr;
    if (!convertLtspiceConditionToStepExpr(triggerExpr, &stepExpr)) return line;

    tail.remove(triggerRe);
    tail = tail.simplified();

    const QString hiddenNode = QString("%1__trigger_src").arg(ref);
    const QString hiddenRef = QString("V__TRIGSRC_%1").arg(ref);
    const QString bufferRef = QString("B__TRIGBUF_%1").arg(ref);

    QStringList rewrittenLines;
    QString hiddenLine = QString("%1 %2 %3 %4").arg(hiddenRef, hiddenNode, nminus, pulseExpr);
    if (!tail.isEmpty()) hiddenLine += " " + tail;
    rewrittenLines << hiddenLine;
    rewrittenLines << QString("%1 %2 %3 V={(%4)*V(%5,%6)}").arg(bufferRef, nplus, nminus, stepExpr, hiddenNode, nminus);

    if (warnings) {
        warnings->append(QString("Approximated LTspice PULSE Trigger= behavior on %1 by gating a hidden pulse source with the trigger expression.").arg(ref));
        warnings->append(QString("LTspice triggered source restart semantics are only partially emulated for %1; the pulse is gated by the trigger but not restarted on each trigger event.").arg(ref));
    }
    return rewrittenLines.join("\n");
}

QString rewriteLtspiceTriggeredPwlSource(const QString& line, QStringList* warnings = nullptr) {
    static const QRegularExpression sourceRe(
        "^\\s*(V\\S*)\\s+(\\S+)\\s+(\\S+)\\s+(.+)$",
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = sourceRe.match(line);
    if (!match.hasMatch()) return line;

    const QString ref = match.captured(1).trimmed();
    const QString nplus = match.captured(2).trimmed();
    const QString nminus = match.captured(3).trimmed();
    const QString rest = match.captured(4).trimmed();
    if (!rest.startsWith("PWL", Qt::CaseInsensitive)) return line;

    static const QRegularExpression triggerRe("\\bTrigger\\s*=\\s*(.+?)(?=\\s+tripd[vt]\\s*=|$)", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch triggerMatch = triggerRe.match(rest);
    if (!triggerMatch.hasMatch()) return line;

    QString stepExpr;
    const QString triggerExpr = triggerMatch.captured(1).trimmed();
    if (!convertLtspiceConditionToStepExpr(triggerExpr, &stepExpr)) return line;

    QString pwlExpr = rest;
    pwlExpr.remove(triggerRe);
    pwlExpr = pwlExpr.simplified();

    const QString hiddenNode = QString("%1__trigger_src").arg(ref);
    const QString hiddenRef = QString("V__TRIGSRC_%1").arg(ref);
    const QString bufferRef = QString("B__TRIGBUF_%1").arg(ref);

    QStringList rewrittenLines;
    rewrittenLines << QString("%1 %2 %3 %4").arg(hiddenRef, hiddenNode, nminus, pwlExpr);
    rewrittenLines << QString("%1 %2 %3 V={(%4)*V(%5,%6)}").arg(bufferRef, nplus, nminus, stepExpr, hiddenNode, nminus);

    if (warnings) {
        warnings->append(QString("Approximated LTspice PWL Trigger= behavior on %1 by gating a hidden PWL source with the trigger expression.").arg(ref));
        warnings->append(QString("LTspice triggered PWL restart semantics are only partially emulated for %1; the waveform is gated by the trigger but not restarted on each trigger event.").arg(ref));
    }
    return rewrittenLines.join("\n");
}

QString rewriteLtspiceTriggeredWaveSource(const QString& line, const QString& kind, QStringList* warnings = nullptr) {
    static const QRegularExpression sourceRe(
        "^\\s*(V\\S*)\\s+(\\S+)\\s+(\\S+)\\s+(.+)$",
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = sourceRe.match(line);
    if (!match.hasMatch()) return line;

    const QString ref = match.captured(1).trimmed();
    const QString nplus = match.captured(2).trimmed();
    const QString nminus = match.captured(3).trimmed();
    const QString rest = match.captured(4).trimmed();
    if (!rest.startsWith(kind, Qt::CaseInsensitive)) return line;

    static const QRegularExpression triggerRe("\\bTrigger\\s*=\\s*(.+?)(?=\\s+tripd[vt]\\s*=|$)", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch triggerMatch = triggerRe.match(rest);
    if (!triggerMatch.hasMatch()) return line;

    QString stepExpr;
    const QString triggerExpr = triggerMatch.captured(1).trimmed();
    if (!convertLtspiceConditionToStepExpr(triggerExpr, &stepExpr)) return line;

    QString sourceExpr = rest;
    sourceExpr.remove(triggerRe);
    sourceExpr = sourceExpr.simplified();

    const QString hiddenNode = QString("%1__trigger_src").arg(ref);
    const QString hiddenRef = QString("V__TRIGSRC_%1").arg(ref);
    const QString bufferRef = QString("B__TRIGBUF_%1").arg(ref);

    QStringList rewrittenLines;
    rewrittenLines << QString("%1 %2 %3 %4").arg(hiddenRef, hiddenNode, nminus, sourceExpr);
    rewrittenLines << QString("%1 %2 %3 V={(%4)*V(%5,%6)}").arg(bufferRef, nplus, nminus, stepExpr, hiddenNode, nminus);

    if (warnings) {
        warnings->append(QString("Approximated LTspice %1 Trigger= behavior on %2 by gating a hidden %1 source with the trigger expression.").arg(kind.toUpper(), ref));
        warnings->append(QString("LTspice triggered %1 restart semantics are only partially emulated for %2; the waveform is gated by the trigger but not restarted on each trigger event.").arg(kind.toUpper(), ref));
    }
    return rewrittenLines.join("\n");
}

QString rewriteLtspiceBehavioralFunctions(const QString& line, QStringList* warnings = nullptr) {
    struct RewriteRule {
        QString name;
        int minArgs;
        int maxArgs;
    };

    const QList<RewriteRule> rules = {
        {"buf", 1, 1},
        {"inv", 1, 1},
        {"uramp", 1, 1},
        {"limit", 3, 3},
        {"dnlim", 3, 3},
        {"uplim", 3, 3},
    };

    QString out = line;
    bool changed = false;

    auto buildReplacement = [](const QString& name, const QStringList& args) {
        if (name.compare("buf", Qt::CaseInsensitive) == 0) {
            return QString("u((%1)-(0.5))").arg(args.at(0));
        }
        if (name.compare("inv", Qt::CaseInsensitive) == 0) {
            return QString("(1-u((%1)-(0.5)))").arg(args.at(0));
        }
        if (name.compare("uramp", Qt::CaseInsensitive) == 0) {
            return QString("((%1)*u(%1))").arg(args.at(0));
        }
        if (name.compare("limit", Qt::CaseInsensitive) == 0) {
            return QString("min(max((%1),min((%2),(%3))),max((%2),(%3)))").arg(args.at(0), args.at(1), args.at(2));
        }
        if (name.compare("dnlim", Qt::CaseInsensitive) == 0) {
            return QString("max((%1),(%2))").arg(args.at(0), args.at(1));
        }
        if (name.compare("uplim", Qt::CaseInsensitive) == 0) {
            return QString("min((%1),(%2))").arg(args.at(0), args.at(1));
        }
        return QString();
    };

    bool replaced = true;
    while (replaced) {
        replaced = false;
        for (const RewriteRule& rule : rules) {
            const QString needle = rule.name + "(";
            const int nameIndex = out.indexOf(needle, 0, Qt::CaseInsensitive);
            if (nameIndex < 0) continue;

            const int openIndex = nameIndex + rule.name.size();
            const int closeIndex = findMatchingParen(out, openIndex);
            if (closeIndex < 0) continue;

            const QString inner = out.mid(openIndex + 1, closeIndex - openIndex - 1);
            const QStringList args = splitTopLevelSpiceArgs(inner);
            if (args.size() < rule.minArgs || args.size() > rule.maxArgs) continue;

            const QString replacement = buildReplacement(rule.name, args);
            if (replacement.isEmpty()) continue;

            out.replace(nameIndex, closeIndex - nameIndex + 1, replacement);
            changed = true;
            replaced = true;
            break;
        }
    }

    if (changed && warnings) {
        warnings->append(QString("Rewrote LTspice behavioral helper functions for ngspice compatibility in: %1").arg(line.trimmed()));
    }

    return out;
}

QStringList tokenizeLtspiceOtaLine(const QString& line) {
    return line.simplified().split(' ', Qt::SkipEmptyParts);
}

QString buildNgspiceOtaTranslation(const QString& line) {
    const QStringList tokens = tokenizeLtspiceOtaLine(line);
    if (tokens.size() < 10) return QString();
    if (tokens.at(9).compare("OTA", Qt::CaseInsensitive) != 0) return QString();
    if (!tokens.at(0).startsWith('A', Qt::CaseInsensitive)) return QString();

    const QString ref = tokens.at(0);
    const QString n1 = tokens.at(1);
    const QString n2 = tokens.at(2);
    const QString n3 = tokens.at(3);
    const QString n4 = tokens.at(4);
    const QString rail = tokens.at(6);
    const QString out = tokens.at(7);
    const QString gnd = tokens.at(8);

    QMap<QString, QString> params;
    QSet<QString> flags;
    for (int i = 10; i < tokens.size(); ++i) {
        const QString token = tokens.at(i).trimmed();
        if (token.isEmpty()) continue;
        const int eq = token.indexOf('=');
        if (eq >= 0) {
            QString key = token.left(eq).trimmed().toLower();
            QString value = token.mid(eq + 1).trimmed();
            if (value.isEmpty() && i + 1 < tokens.size()) {
                value = tokens.at(++i).trimmed();
            }
            if (!key.isEmpty()) params.insert(key, value);
        } else {
            flags.insert(token.toLower());
        }
    }

    const QString gm = params.value("g", "1u");
    const QString refExpr = params.value("ref", "0");
    const QString upper = params.contains("iout") ? params.value("iout")
                       : params.contains("isrc") ? params.value("isrc")
                       : QStringLiteral("10u");
    const QString lower = params.contains("isink") ? params.value("isink")
                       : QStringLiteral("-(%1)").arg(upper);
    const QString rout = params.value("rout").trimmed();
    const QString cout = params.value("cout").trimmed();
    const QString vhigh = params.value("vhigh").trimmed();
    const QString vlow = params.value("vlow").trimmed();
    const QString epsilon = params.value("epsilon", "1u").trimmed();

    const QString diffExpr = QString("(((V(%1,%2))+(V(%3,%4)))-(%5))")
        .arg(n1, n2, n3, n4, refExpr);
    const QString rawExpr = QString("((%1)*(%2))").arg(gm, diffExpr);

    QString currentExpr;
    if (flags.contains("linear")) {
        currentExpr = QString("min(max((%1),(%2)),(%3))").arg(rawExpr, lower, upper);
    } else {
        const QString posExpr = QString("(u(%1)*((%2)*tanh((%1)/(max(abs((%2)),1e-30)))))")
            .arg(rawExpr, upper);
        const QString negExpr = QString("(u(-(%1))*((abs((%2)))*tanh((-(%1))/(max(abs((%2)),1e-30)))))")
            .arg(rawExpr, lower);
        currentExpr = QString("((%1)-(%2))").arg(posExpr, negExpr);
    }

    if (!vhigh.isEmpty() || !vlow.isEmpty()) {
        const QString highExpr = vhigh.isEmpty() ? QStringLiteral("1e308")
                                                 : QString("((V(%1,%2))+(%3))").arg(rail, gnd, vhigh);
        const QString lowExpr = vlow.isEmpty() ? QStringLiteral("-1e308")
                                               : QString("((V(%1,%2))+(%3))").arg(gnd, gnd, vlow);
        const QString voutExpr = QString("V(%1,%2)").arg(out, gnd);
        const QString compliance = QString("(u((%1)-(%2)+(%3))*u((%2)-(%4)+(%3)))")
            .arg(highExpr, voutExpr, epsilon, lowExpr);
        currentExpr = QString("((%1)*(%2))").arg(currentExpr, compliance);
    }

    QStringList lines;
    lines << QString("* OTA_TRANSLATED %1").arg(ref);
    lines << QString("B__OTA_%1 %2 %3 I={%4}").arg(sanitizeMixedModeToken(ref), out, gnd, currentExpr);
    if (!rout.isEmpty()) {
        lines << QString("R__OTA_%1 %2 %3 %4").arg(sanitizeMixedModeToken(ref), out, gnd, rout);
    }
    if (!cout.isEmpty()) {
        lines << QString("C__OTA_%1 %2 %3 %4").arg(sanitizeMixedModeToken(ref), out, gnd, cout);
    }
    return lines.join('\n');
}

QString rewriteUnsupportedLtspiceBehavioralTimeFunctions(const QString& line, QStringList* warnings = nullptr) {
    QString out = line;
    struct FuncSpec { QString name; int minArgs; int maxArgs; };
    const QList<FuncSpec> funcs = {
        {"absdelay", 2, 3},
        {"delay", 2, 3},
    };

    bool changed = false;
    bool replaced = true;
    while (replaced) {
        replaced = false;
        for (const FuncSpec& func : funcs) {
            const int nameIndex = out.indexOf(QRegularExpression(QString("\\b%1\\s*\\(").arg(QRegularExpression::escape(func.name)),
                                                                    QRegularExpression::CaseInsensitiveOption));
            if (nameIndex < 0) continue;
            const int openIndex = out.indexOf('(', nameIndex);
            const int closeIndex = findMatchingParen(out, openIndex);
            if (closeIndex < 0) continue;

            const QString inner = out.mid(openIndex + 1, closeIndex - openIndex - 1);
            const QStringList args = splitTopLevelSpiceArgs(inner);
            if (args.size() < func.minArgs || args.size() > func.maxArgs) continue;

            const QString passthroughExpr = QString("(%1)").arg(args.at(0).trimmed());
            out.replace(nameIndex, closeIndex - nameIndex + 1, passthroughExpr);
            changed = true;
            replaced = true;
            if (warnings) {
                warnings->append(QString("Approximated LTspice %1(...) by passing through its input expression because this ngspice configuration does not support %1(...). Original line: %2")
                                     .arg(func.name, line.trimmed()));
            }
            break;
        }
    }

    return out;
}

QString rewriteUnsupportedLtspiceTableFunction(const QString& line, QStringList* warnings = nullptr) {
    QString out = line;
    bool changed = false;

    while (true) {
        const int tableIndex = out.indexOf(QRegularExpression("\\btable\\s*\\(", QRegularExpression::CaseInsensitiveOption));
        if (tableIndex < 0) break;
        const int openIndex = out.indexOf('(', tableIndex);
        const int closeIndex = findMatchingParen(out, openIndex);
        if (openIndex < 0 || closeIndex < 0) break;

        const QString inner = out.mid(openIndex + 1, closeIndex - openIndex - 1);
        const QStringList args = splitTopLevelSpiceArgs(inner);
        if (args.size() < 3) break;

        const QString xExpr = args.at(0).trimmed();
        QString replacement;
        if ((args.size() - 1) % 2 == 0) {
            QString expr = args.last().trimmed();
            for (int i = args.size() - 3; i >= 1; i -= 2) {
                const QString xk = args.at(i).trimmed();
                const QString yk = args.at(i + 1).trimmed();
                expr = QString("if((%1)<=(%2),(%3),(%4))").arg(xExpr, xk, yk, expr);
            }
            replacement = expr;
        } else {
            replacement = xExpr;
            if (warnings) {
                warnings->append(QString("LTspice table(...) include/file form is not supported; approximated by passing through the lookup input expression in: %1").arg(line.trimmed()));
            }
        }

        out.replace(tableIndex, closeIndex - tableIndex + 1, replacement);
        changed = true;
    }

    if (changed && warnings) {
        warnings->append(QString("Approximated LTspice table(...) with nested conditional interpolation for ngspice compatibility in: %1").arg(line.trimmed()));
    }
    return out;
}

QString buildCurrentTableExpr(const QString& xExpr, const QStringList& args, QString* error = nullptr) {
    if (args.size() < 2 || (args.size() % 2) != 0) {
        if (error) *error = "Current-source table requires voltage/current pairs.";
        return QString();
    }

    QString expr = args.last().trimmed();
    for (int i = args.size() - 4; i >= 0; i -= 2) {
        const QString x0 = args.at(i).trimmed();
        const QString y0 = args.at(i + 1).trimmed();
        const QString x1 = args.at(i + 2).trimmed();
        const QString y1 = args.at(i + 3).trimmed();
        QString segment = y1;
        if (x0.compare(x1, Qt::CaseInsensitive) != 0) {
            segment = QString("((%1)+((%2)-(%3))*(((%4)-(%5))/((%6)-(%7))))")
                          .arg(y0, y1, y0, xExpr, x0, x1, x0);
        }
        expr = QString("if((%1)<=(%2),(%3),(%4))").arg(xExpr, x1, segment, expr);
    }
    return expr;
}

QString rewriteUnsupportedLtspiceStochasticFunctions(const QString& line, QStringList* warnings = nullptr) {
    QString out = line;
    struct FuncSpec { QString name; int minArgs; int maxArgs; QString replacement; };
    const QList<FuncSpec> funcs = {
        {"rand", 1, 1, "0"},
        {"random", 1, 1, "0"},
        {"white", 1, 1, "0"},
        {"smallsig", 0, 0, "0"},
    };

    bool replaced = true;
    while (replaced) {
        replaced = false;
        for (const FuncSpec& func : funcs) {
            const int funcIndex = out.indexOf(QRegularExpression(QString("\\b%1\\s*\\(").arg(QRegularExpression::escape(func.name)),
                                                                   QRegularExpression::CaseInsensitiveOption));
            if (funcIndex < 0) continue;
            const int openIndex = out.indexOf('(', funcIndex);
            const int closeIndex = findMatchingParen(out, openIndex);
            if (openIndex < 0 || closeIndex < 0) continue;
            const QString inner = out.mid(openIndex + 1, closeIndex - openIndex - 1);
            const QStringList args = inner.trimmed().isEmpty() ? QStringList() : splitTopLevelSpiceArgs(inner);
            if (args.size() < func.minArgs || args.size() > func.maxArgs) continue;

            out.replace(funcIndex, closeIndex - funcIndex + 1, func.replacement);
            replaced = true;
            if (warnings) {
                warnings->append(QString("Approximated LTspice %1(...) as 0 because this ngspice configuration does not support %1(...). Original line: %2")
                                     .arg(func.name, line.trimmed()));
            }
            break;
        }
    }

    return out;
}

QString rewriteLtspiceBSourceLaplaceOptions(const QString& line, QStringList* warnings = nullptr) {
    static const QRegularExpression bSourceRe(
        "^\\s*(B\\S+)\\s+\\S+\\s+\\S+\\s+(?:V|I|R|P)\\s*=.*$",
        QRegularExpression::CaseInsensitiveOption);
    if (!bSourceRe.match(line).hasMatch()) return line;

    QString out = line;
    const QString original = line.trimmed();

    static const QRegularExpression laplaceRe("\\blaplace\\s*=\\s*(\\{[^}]*\\}|\\S+)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression windowRe("\\bwindow\\s*=\\s*\\S+", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression nfftRe("\\bnfft\\s*=\\s*\\S+", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression mtolRe("\\bmtol\\s*=\\s*\\S+", QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch laplaceMatch = laplaceRe.match(out);
    if (!laplaceMatch.hasMatch()) return line;

    const QString laplaceExpr = laplaceMatch.captured(1).trimmed();
    out.remove(laplaceRe);
    out.remove(windowRe);
    out.remove(nfftRe);
    out.remove(mtolRe);
    out = out.simplified();

    if (warnings) {
        warnings->append(QString("Dropped LTspice B-source laplace= transform from %1 because this ngspice configuration does not accept LTspice-style Laplace options on B-sources.").arg(original));
        warnings->append(QString("Preserved the underlying behavioral source but removed laplace/window/nfft/mtol options; resulting behavior may differ from LTspice. Dropped Laplace expression: %1").arg(laplaceExpr));
    }

    return out;
}

QString rewriteLtspiceBehavioralSourceRpar(const QString& line, QStringList* warnings = nullptr) {
    static const QRegularExpression bSourceRe(
        "^\\s*(B\\S+)\\s+(\\S+)\\s+(\\S+)\\s+([IR])\\s*=\\s*(.+)$",
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = bSourceRe.match(line);
    if (!match.hasMatch()) return line;

    QString out = line;
    const QString ref = match.captured(1).trimmed();
    const QString nplus = match.captured(2).trimmed();
    const QString nminus = match.captured(3).trimmed();
    const QString mode = match.captured(4).trimmed().toUpper();
    QString exprAndTail = match.captured(5).trimmed();

    static const QRegularExpression rparRe("\\bRpar\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch rparMatch = rparRe.match(exprAndTail);
    if (!rparMatch.hasMatch()) return line;

    const QString rparValue = rparMatch.captured(1).trimmed();
    exprAndTail.remove(rparRe);
    exprAndTail = exprAndTail.simplified();

    const QString shuntRef = QString("R__RPAR_%1").arg(ref);
    QStringList rewrittenLines;
    rewrittenLines << QString("%1 %2 %3 %4=%5").arg(ref, nplus, nminus, mode, exprAndTail);
    rewrittenLines << QString("%1 %2 %3 %4").arg(shuntRef, nplus, nminus, rparValue);
    out = rewrittenLines.join("\n");

    if (warnings) {
        warnings->append(QString("Expanded LTspice behavioral source Rpar= on %1 into explicit shunt resistor for ngspice.").arg(ref));
    }
    return out;
}

QString rewriteLtspiceSourceTripOptions(const QString& line, QStringList* warnings = nullptr) {
    static const QRegularExpression sourceRe(
        "^\\s*([VI]\\S*)\\s+(\\S+)\\s+(\\S+)\\s+(.+)$",
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = sourceRe.match(line);
    if (!match.hasMatch()) return line;

    const QString ref = match.captured(1).trimmed();
    QString rest = match.captured(4).trimmed();
    if (!(rest.startsWith("PULSE", Qt::CaseInsensitive) || rest.startsWith("PWL", Qt::CaseInsensitive) ||
          rest.startsWith("SINE", Qt::CaseInsensitive) || rest.startsWith("EXP", Qt::CaseInsensitive) ||
          rest.startsWith("SFFM", Qt::CaseInsensitive))) {
        return line;
    }

    static const QRegularExpression tripdvRe("\\btripdv\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tripdtRe("\\btripdt\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch tripdvMatch = tripdvRe.match(rest);
    const QRegularExpressionMatch tripdtMatch = tripdtRe.match(rest);
    if (!tripdvMatch.hasMatch() && !tripdtMatch.hasMatch()) return line;

    const QString tripdv = tripdvMatch.hasMatch() ? tripdvMatch.captured(1).trimmed() : QString();
    const QString tripdt = tripdtMatch.hasMatch() ? tripdtMatch.captured(1).trimmed() : QString();
    rest.remove(tripdvRe);
    rest.remove(tripdtRe);
    rest = rest.simplified();

    const QString out = QString("%1 %2 %3 %4")
                            .arg(ref, match.captured(2).trimmed(), match.captured(3).trimmed(), rest);
    if (warnings) {
        warnings->append(QString("Dropped LTspice source tripdv=/tripdt= options from %1 because this ngspice configuration rejects them on independent sources.").arg(ref));
        warnings->append(QString("Removed step-rejection options from %1: tripdv=%2 tripdt=%3").arg(
            ref,
            tripdv.isEmpty() ? QString("<none>") : tripdv,
            tripdt.isEmpty() ? QString("<none>") : tripdt));
    }
    return out;
}

QString rewriteLtspiceBSourceTripOptions(const QString& line, QStringList* warnings = nullptr) {
    static const QRegularExpression bSourceRe(
        "^\\s*(B\\S+)\\s+(\\S+)\\s+(\\S+)\\s+([VIRP])\\s*=\\s*(.+)$",
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = bSourceRe.match(line);
    if (!match.hasMatch()) return line;

    const QString ref = match.captured(1).trimmed();
    QString tail = match.captured(5).trimmed();

    static const QRegularExpression tripdvRe("\\btripdv\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tripdtRe("\\btripdt\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch tripdvMatch = tripdvRe.match(tail);
    const QRegularExpressionMatch tripdtMatch = tripdtRe.match(tail);
    if (!tripdvMatch.hasMatch() && !tripdtMatch.hasMatch()) return line;

    const QString tripdv = tripdvMatch.hasMatch() ? tripdvMatch.captured(1).trimmed() : QString();
    const QString tripdt = tripdtMatch.hasMatch() ? tripdtMatch.captured(1).trimmed() : QString();
    tail.remove(tripdvRe);
    tail.remove(tripdtRe);
    tail = tail.simplified();

    const QString out = QString("%1 %2 %3 %4=%5")
                            .arg(ref, match.captured(2).trimmed(), match.captured(3).trimmed(), match.captured(4).trimmed(), tail);
    if (warnings) {
        warnings->append(QString("Dropped LTspice B-source tripdv=/tripdt= options from %1 because this ngspice configuration rejects them on behavioral sources.").arg(ref));
        warnings->append(QString("Removed B-source step-rejection options from %1: tripdv=%2 tripdt=%3").arg(
            ref,
            tripdv.isEmpty() ? QString("<none>") : tripdv,
            tripdt.isEmpty() ? QString("<none>") : tripdt));
    }
    return out;
}

void appendLtspiceBSourceOptionWarnings(const QString& line, QStringList* warnings) {
    if (!warnings) return;

    static const QRegularExpression bSourceRe(
        "^\\s*(B\\S+)\\s+\\S+\\s+\\S+\\s+(?:V|I|R|P)\\s*=.*$",
        QRegularExpression::CaseInsensitiveOption);
    if (!bSourceRe.match(line).hasMatch()) return;

    const QString trimmed = line.trimmed();
    if (trimmed.contains(QRegularExpression("\\bic\\s*=", QRegularExpression::CaseInsensitiveOption))) {
        warnings->append(QString("LTspice B-source instance option ic= detected and passed through unchanged: %1").arg(trimmed));
    }
    if (trimmed.contains(QRegularExpression("\\bvprx\\s*=", QRegularExpression::CaseInsensitiveOption))) {
        warnings->append(QString("LTspice behavioral power-source option vprx= detected and passed through unchanged; ngspice compatibility may differ: %1").arg(trimmed));
    }
    if (trimmed.contains(QRegularExpression("\\btripdv\\s*=", QRegularExpression::CaseInsensitiveOption)) ||
        trimmed.contains(QRegularExpression("\\btripdt\\s*=", QRegularExpression::CaseInsensitiveOption))) {
        warnings->append(QString("LTspice B-source step-rejection options tripdv=/tripdt= detected; VioSpice will drop them if needed to keep ngspice loadable: %1").arg(trimmed));
    }
    if (trimmed.contains(QRegularExpression("\\blaplace\\s*=", QRegularExpression::CaseInsensitiveOption)) ||
        trimmed.contains(QRegularExpression("\\bwindow\\s*=", QRegularExpression::CaseInsensitiveOption)) ||
        trimmed.contains(QRegularExpression("\\bnfft\\s*=", QRegularExpression::CaseInsensitiveOption)) ||
        trimmed.contains(QRegularExpression("\\bmtol\\s*=", QRegularExpression::CaseInsensitiveOption))) {
        warnings->append(QString("LTspice B-source Laplace options detected; VioSpice will drop them if needed to keep ngspice loadable: %1").arg(trimmed));
    }
}

void appendLtspiceSourceOptionWarnings(const QString& line, QStringList* warnings) {
    if (!warnings) return;

    static const QRegularExpression sourceRe(
        "^\\s*([VI]\\S*)\\s+\\S+\\s+\\S+\\s+(.+)$",
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = sourceRe.match(line);
    if (!match.hasMatch()) return;

    const QString ref = match.captured(1).trimmed();
    const QString rest = match.captured(2).trimmed();
    if (!(rest.startsWith("PULSE", Qt::CaseInsensitive) || rest.startsWith("PWL", Qt::CaseInsensitive) ||
          rest.startsWith("SINE", Qt::CaseInsensitive) || rest.startsWith("EXP", Qt::CaseInsensitive) ||
          rest.startsWith("SFFM", Qt::CaseInsensitive))) {
        return;
    }

        if (rest.contains(QRegularExpression("\\bTrigger\\s*=", QRegularExpression::CaseInsensitiveOption))) {
            if (rest.startsWith("PULSE", Qt::CaseInsensitive)) {
                warnings->append(QString("LTspice PULSE Trigger= detected on %1; VioSpice will approximate it by gating a hidden pulse source.").arg(ref));
            } else if (rest.startsWith("PWL", Qt::CaseInsensitive)) {
                warnings->append(QString("LTspice PWL Trigger= detected on %1; VioSpice will approximate it by gating a hidden PWL source.").arg(ref));
            } else if (rest.startsWith("SINE", Qt::CaseInsensitive) || rest.startsWith("EXP", Qt::CaseInsensitive) ||
                       rest.startsWith("SFFM", Qt::CaseInsensitive)) {
                warnings->append(QString("LTspice %1 Trigger= detected on %2; VioSpice will approximate it by gating a hidden %1 source.")
                                     .arg(rest.section('(', 0, 0).trimmed().toUpper(), ref));
            } else {
                warnings->append(QString("LTspice triggered source restart semantics are not yet emulated for %1; Trigger= is passed through unchanged: %2").arg(ref, line.trimmed()));
            }
        }
    if (rest.contains(QRegularExpression("\\btripdv\\s*=", QRegularExpression::CaseInsensitiveOption)) ||
        rest.contains(QRegularExpression("\\btripdt\\s*=", QRegularExpression::CaseInsensitiveOption))) {
        warnings->append(QString("LTspice source step-rejection options tripdv=/tripdt= detected on %1; VioSpice will drop them if needed to keep ngspice loadable: %2").arg(ref, line.trimmed()));
    }
}

QString rewriteLtspiceStartupSourceLine(const QString& line, QStringList* warnings = nullptr) {
    static const QString startupScaleExpr = "min(1,max(0,time/20u))";
    static const QRegularExpression simpleValueRe(
        "^(?:DC\\s+)?(\\{[^}]+\\}|[-+]?\\d+(?:\\.\\d+)?(?:[eE][-+]?\\d+)?[a-zA-Z]*)$",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression voltageSourceRe(
        "^\\s*(V\\S*)\\s+(\\S+)\\s+(\\S+)\\s+(.+?)\\s*$",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression currentDcSourceRe(
        "^\\s*(I\\S*)\\s+(\\S+)\\s+(\\S+)\\s+(?:DC\\s+)?(\\{[^}]+\\}|[-+]?\\d+(?:\\.\\d+)?(?:[eE][-+]?\\d+)?[a-zA-Z]*)\\s*$",
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch voltageMatch = voltageSourceRe.match(line);
    if (voltageMatch.hasMatch()) {
        const QString ref = voltageMatch.captured(1).trimmed();
        const QString nplus = voltageMatch.captured(2).trimmed();
        const QString nminus = voltageMatch.captured(3).trimmed();
        const QString value = voltageMatch.captured(4).trimmed();

        const QRegularExpressionMatch simpleValueMatch = simpleValueRe.match(value);
        if (simpleValueMatch.hasMatch()) {
            const QString targetValue = simpleValueMatch.captured(1).trimmed();
            const QString rewritten = QString("%1 %2 %3 PWL(0 0 20u %4)").arg(ref, nplus, nminus, targetValue);
            if (warnings) {
                warnings->append(QString("Approximated LTspice startup behavior by ramping voltage source %1 from 0 to its target over 20us.").arg(ref));
            }
            return rewritten;
        }

        const QString hiddenNode = QString("%1__startup").arg(ref);
        const QString hiddenRef = QString("V__STARTUPSRC_%1").arg(ref);
        const QString bufferRef = QString("B__STARTUPBUF_%1").arg(ref);

        QStringList rewrittenLines;
        rewrittenLines << QString("%1 %2 %3 %4").arg(hiddenRef, hiddenNode, nminus, value);
        rewrittenLines << QString("%1 %2 %3 V={(%4)*V(%5,%6)}").arg(bufferRef, nplus, nminus, startupScaleExpr, hiddenNode, nminus);
        if (warnings) {
            warnings->append(QString("Approximated LTspice startup behavior by scaling voltage source %1 from 0 to full amplitude over 20us.").arg(ref));
        }
        return rewrittenLines.join("\n");
    }

    const QRegularExpressionMatch currentMatch = currentDcSourceRe.match(line);
    if (!currentMatch.hasMatch()) return line;

    const QString ref = currentMatch.captured(1).trimmed();
    const QString nplus = currentMatch.captured(2).trimmed();
    const QString nminus = currentMatch.captured(3).trimmed();
    const QString value = currentMatch.captured(4).trimmed();
    const QString rewritten = QString("%1 %2 %3 PWL(0 0 20u %4)").arg(ref, nplus, nminus, value);
    if (warnings) {
        warnings->append(QString("Approximated LTspice startup behavior by ramping current source %1 from 0 to its target over 20us.").arg(ref));
    }
    return rewritten;
}

QString rewriteLtspiceDirectiveLine(const QString& line, QStringList* warnings = nullptr, bool emulateStartup = false, const QString& projectDir = QString()) {
    QString out = line;

    appendLtspiceBSourceOptionWarnings(out, warnings);
    appendLtspiceSourceOptionWarnings(out, warnings);

    out = rewriteLtspiceBSourceTripOptions(out, warnings);
    out = rewriteLtspiceBSourceLaplaceOptions(out, warnings);
    out = rewriteLtspiceBehavioralSourceRpar(out, warnings);
    out = rewriteLtspiceSourceTripOptions(out, warnings);
    out = rewriteLtspiceTriggeredPulseSource(out, warnings);
    out = rewriteLtspiceTriggeredPwlSource(out, warnings);
    out = rewriteLtspiceTriggeredWaveSource(out, "SINE", warnings);
    out = rewriteLtspiceTriggeredWaveSource(out, "EXP", warnings);
    out = rewriteLtspiceTriggeredWaveSource(out, "SFFM", warnings);
    out = rewriteLtspiceVoltageSourceExtras(out, warnings);

    {
        static const QRegularExpression sourceRe(
            "^\\s*([VI]\\S*)\\s+(\\S+)\\s+(\\S+)\\s+(.+)$",
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch sourceMatch = sourceRe.match(out);
        if (sourceMatch.hasMatch()) {
            const QString ref = sourceMatch.captured(1);
            const QString nplus = sourceMatch.captured(2);
            const QString nminus = sourceMatch.captured(3);
            const QString prefix = QString("%1 %2 %3 ").arg(ref, nplus, nminus);
            const QString rest = sourceMatch.captured(4).trimmed();
            if (ref.startsWith('I', Qt::CaseInsensitive)) {
                QString rewritten;
                if (rewriteLtspiceCurrentSourceSpecial(ref, nplus, nminus, rest, projectDir, &rewritten, warnings)) {
                    out = rewritten;
                    return out;
                }
            }
            
            // Optimization: If 'rest' already looks like expanded PWL (starts with PWL and contains 
            // many tokens or newlines), don't try to inline it again.
            if (rest.startsWith("PWL", Qt::CaseInsensitive) && (rest.contains('\n') || rest.split(' ').size() > 10)) {
                return out;
            }

            const QString normalizedRest = inlinePwlFileIfNeeded(rest, projectDir, warnings);
            out = prefix + normalizedRest;
        }
    }

    if (emulateStartup) {
        QStringList startupLines;
        for (const QString& part : out.split('\n')) {
            startupLines << rewriteLtspiceStartupSourceLine(part, warnings);
        }
        out = startupLines.join("\n");
    }

    if (out.trimmed().compare(".end", Qt::CaseInsensitive) == 0) {
        if (warnings) {
            warnings->append(QString("Dropped .end from directive block; VioSpice appends the final .end automatically."));
        }
        return QString();
    }

    {
        static const QRegularExpression bSourceRe(
            "^\\s*(B\\S+)\\s+(\\S+)\\s+(\\S+)\\s+V\\s*=\\s*(.+)$",
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch bMatch = bSourceRe.match(out);
        if (bMatch.hasMatch() && (out.contains("idt(", Qt::CaseInsensitive) || out.contains("sdt(", Qt::CaseInsensitive) || out.contains("idtmod(", Qt::CaseInsensitive))) {
            const QString ref = bMatch.captured(1).trimmed();
            const QString nplus = bMatch.captured(2).trimmed();
            const QString nminus = bMatch.captured(3).trimmed();
            QString expr = bMatch.captured(4).trimmed();

            // Strip outer braces if present for cleaner parsing
            bool hadBraces = false;
            if (expr.startsWith('{') && expr.endsWith('}')) {
                expr = expr.mid(1, expr.length() - 2).trimmed();
                hadBraces = true;
            }

            static const QRegularExpression idtTailRe(
                "^(.*?)(?:([+\\-])\\s*)?([^\\s+\\-]+)?\\s*\\*?\\s*(idt|sdt|idtmod)\\s*\\((.*)\\)\\s*$",
                QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch idtMatch = idtTailRe.match(expr);
            if (idtMatch.hasMatch()) {
                QString prefix = idtMatch.captured(1).trimmed();
                const QString sign = idtMatch.captured(2).trimmed();
                QString coeff = idtMatch.captured(3).trimmed();
                const QString funcName = idtMatch.captured(4).trimmed();
                const QStringList idtArgs = splitTopLevelSpiceArgs(idtMatch.captured(5).trimmed());
                const QString innerExpr = idtArgs.value(0).trimmed();
                const QString icExpr = idtArgs.value(1).trimmed().isEmpty() ? "0" : idtArgs.value(1).trimmed();
                const bool isIdtMod = funcName.compare("idtmod", Qt::CaseInsensitive) == 0;
                const QString assertExpr = isIdtMod ? QString() : idtArgs.value(2).trimmed();
                const QString modulusExpr = isIdtMod ? (idtArgs.value(2).trimmed().isEmpty() ? "1" : idtArgs.value(2).trimmed()) : QString();
                const QString offsetExpr = isIdtMod ? (idtArgs.value(3).trimmed().isEmpty() ? "0" : idtArgs.value(3).trimmed()) : QString();
                if (innerExpr.isEmpty()) return out;
                coeff.remove(QRegularExpression("\\*+$"));
                if (coeff.isEmpty()) coeff = "1";
                if (sign == "-") coeff = QString("-(%1)").arg(coeff);
                const QString intNode = QString("%1__idt").arg(ref);
                const QString intDrvRef = QString("B__INTDRV_%1").arg(ref);
                const QString intCapRef = QString("C__INT_%1").arg(ref);
                const QString intLeakRef = QString("R__INTLEAK_%1").arg(ref);
                const QString intResetRef = QString("B__INTRESET_%1").arg(ref);

                QString driveExpr = QString("(%1)*(%2)").arg(coeff, innerExpr);
                if (!assertExpr.isEmpty()) {
                    const QString resetGateExpr = QString("u((%1)-(0.5))").arg(assertExpr);
                    driveExpr = QString("(1-(%1))*(%2)").arg(resetGateExpr, driveExpr);
                }

                QString rewrittenExpr = prefix;
                if (rewrittenExpr.isEmpty()) {
                    if (isIdtMod) {
                        rewrittenExpr = QString("((%1)+((V(%2)-(%1))-(%3)*floor(((V(%2)-(%1))/(%3)))))")
                                            .arg(offsetExpr, intNode, modulusExpr);
                    } else {
                        rewrittenExpr = QString("V(%1)").arg(intNode);
                    }
                } else {
                    // Ensure prefix doesn't have unbalanced braces after split
                    if (prefix.count('{') > prefix.count('}')) prefix += "}";
                    if (prefix.count('}') > prefix.count('{')) prefix.prepend("{");
                    if (isIdtMod) {
                        rewrittenExpr = prefix + QString(" + ((%1)+((V(%2)-(%1))-(%3)*floor(((V(%2)-(%1))/(%3)))))")
                                                   .arg(offsetExpr, intNode, modulusExpr);
                    } else {
                        rewrittenExpr = prefix + QString(" + V(%1)").arg(intNode);
                    }
                }

                // If the original had braces, ensure the new one does too (handled by the bExprRe block usually, 
                // but we might be multiline now, so bExprRe won't match the whole 'out')
                if (hadBraces && !rewrittenExpr.startsWith('{')) {
                    rewrittenExpr = "{" + rewrittenExpr + "}";
                }

                QStringList rewrittenLines;
                rewrittenLines << QString("%1 0 %2 I=%3").arg(intDrvRef, intNode, driveExpr);
                if (!assertExpr.isEmpty()) {
                    const QString resetGateExpr = QString("u((%1)-(0.5))").arg(assertExpr);
                    rewrittenLines << QString("%1 0 %2 I=(%3)*(1e6)*((%4)-V(%2))").arg(intResetRef, intNode, resetGateExpr, icExpr);
                }
                rewrittenLines << QString("%1 %2 0 1").arg(intCapRef, intNode);
                rewrittenLines << QString("%1 %2 0 1G").arg(intLeakRef, intNode);
                rewrittenLines << QString(".ic V(%1)=%2").arg(intNode, icExpr);
                rewrittenLines << QString("%1 %2 %3 V=%4").arg(ref, nplus, nminus, rewrittenExpr);
                out = rewrittenLines.join("\n");
                if (warnings) {
                    warnings->append(QString("Expanded LTspice %1(...) in %2 into an explicit behavioral integrator for ngspice.").arg(funcName.toLower(), ref));
                    if (idtArgs.size() >= 2) {
                        warnings->append(QString("Preserved LTspice %1 initial condition for %2 as %3.").arg(funcName.toLower(), ref, icExpr));
                    }
                    if (!assertExpr.isEmpty()) {
                        warnings->append(QString("Approximated LTspice %1 reset/assert argument for %2 using a behavioral reset clamp.").arg(funcName.toLower(), ref));
                    }
                    if (isIdtMod) {
                        warnings->append(QString("Approximated LTspice idtmod(...) for %1 by wrapping the explicit integrator output with modulus %2 and offset %3.").arg(ref, modulusExpr, offsetExpr));
                    }
                }
            }
        }
    }

    {
        // Handle multiline B-sources if idt expansion happened
        QStringList lines = out.split('\n');
        for (int i = 0; i < lines.size(); ++i) {
            static const QRegularExpression bExprRe(
                "^\\s*(B\\S+\\s+\\S+\\s+\\S+\\s+)([VI])\\s*=\\s*(.+)$",
                QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch bExprMatch = bExprRe.match(lines[i]);
            if (bExprMatch.hasMatch()) {
                const QString head = bExprMatch.captured(1);
                const QString kind = bExprMatch.captured(2);
                QString expr = bExprMatch.captured(3).trimmed();
                if (!(expr.startsWith('{') && expr.endsWith('}'))) {
                    lines[i] = QString("%1%2={%3}").arg(head, kind, expr);
                    if (warnings && !out.contains("\n")) { // Only warn once for simple lines
                        warnings->append(QString("Wrapped LTspice-style behavioral source expression in braces for ngspice: %1").arg(line.trimmed()));
                    }
                }
            }
        }
        out = lines.join("\n");
    }

    if (out.contains("if(", Qt::CaseInsensitive) && out.contains("={", Qt::CaseInsensitive)) {
        out = rewriteLtspiceBehavioralIf(out, warnings);
    }

    if ((out.contains("buf(", Qt::CaseInsensitive) || out.contains("inv(", Qt::CaseInsensitive) ||
         out.contains("uramp(", Qt::CaseInsensitive) || out.contains("limit(", Qt::CaseInsensitive) ||
         out.contains("dnlim(", Qt::CaseInsensitive) || out.contains("uplim(", Qt::CaseInsensitive) ||
         out.contains("idtmod(", Qt::CaseInsensitive)) && out.contains("={", Qt::CaseInsensitive)) {
        out = rewriteLtspiceBehavioralFunctions(out, warnings);
    }

    if ((out.contains("delay(", Qt::CaseInsensitive) || out.contains("absdelay(", Qt::CaseInsensitive)) && out.contains("={", Qt::CaseInsensitive)) {
        out = rewriteUnsupportedLtspiceBehavioralTimeFunctions(out, warnings);
    }

    if (out.contains("table(", Qt::CaseInsensitive) && out.contains("={", Qt::CaseInsensitive)) {
        out = rewriteUnsupportedLtspiceTableFunction(out, warnings);
        if (out.contains("if(", Qt::CaseInsensitive)) {
            out = rewriteLtspiceBehavioralIf(out, warnings);
        }
    }

    if ((out.contains("rand(", Qt::CaseInsensitive) || out.contains("random(", Qt::CaseInsensitive) ||
         out.contains("white(", Qt::CaseInsensitive) || out.contains("smallsig(", Qt::CaseInsensitive)) &&
        out.contains("={", Qt::CaseInsensitive)) {
        out = rewriteUnsupportedLtspiceStochasticFunctions(out, warnings);
    }

    if (out.contains(" V={", Qt::CaseInsensitive)) {
        const QString original = out;
        out.replace("&&", " and ");
        out.replace("||", " or ");

        static const QRegularExpression singleAndRe("(?<![&])&(?![&])");
        static const QRegularExpression singleOrRe("(?<![|])\\|(?![|])");
        out.replace(singleAndRe, " and ");
        out.replace(singleOrRe, " or ");

        if (out != original && warnings) {
            warnings->append(QString("Rewrote LTspice-style boolean operators to ngspice-safe logical operators in: %1").arg(line.trimmed()));
        }
    }

    {
        static const QRegularExpression passiveRserRe(
            "^\\s*([CL][^\\s]*)\\s+(\\S+)\\s+(\\S+)\\s+(\\S+)(.*\\bRser\\s*=\\s*([^\\s]+).*)$",
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch passiveMatch = passiveRserRe.match(out);
        if (passiveMatch.hasMatch()) {
            const QString ref = passiveMatch.captured(1).trimmed();
            const QString node1 = passiveMatch.captured(2).trimmed();
            const QString node2 = passiveMatch.captured(3).trimmed();
            const QString value = passiveMatch.captured(4).trimmed();
            QString extras = passiveMatch.captured(5);
            const QString rser = passiveMatch.captured(6).trimmed();

            extras.remove(QRegularExpression("\\bRser\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption));
            extras = extras.simplified();

            const QString insertedNode = QString("%1__rser").arg(ref);
            const QString seriesRef = QString("R__RSER_%1").arg(ref);
            QStringList rewrittenLines;
            rewrittenLines << QString("%1 %2 %3 %4").arg(seriesRef, node1, insertedNode, rser);

            QString passiveLine = QString("%1 %2 %3 %4").arg(ref, insertedNode, node2, value);
            if (!extras.isEmpty()) passiveLine += " " + extras;
            rewrittenLines << passiveLine;
            out = rewrittenLines.join("\n");
            if (warnings) {
                warnings->append(QString("Expanded LTspice inline Rser= on %1 into explicit series resistor for ngspice.").arg(ref));
            }
        }
    }

    {
        static const QRegularExpression diodeModelRe(
            "^\\s*\\.model\\s+(\\S+)\\s+D\\((.*)\\)\\s*$",
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch diodeMatch = diodeModelRe.match(out);
        if (diodeMatch.hasMatch()) {
            QString body = diodeMatch.captured(2);
            const QString originalBody = body;
            body.replace(QRegularExpression("\\bRon\\s*=", QRegularExpression::CaseInsensitiveOption), "Rs=");
            body.remove(QRegularExpression("(?:^|\\s)Roff\\s*=\\s*[^\\s)]+", QRegularExpression::CaseInsensitiveOption));
            body.remove(QRegularExpression("(?:^|\\s)Vfwd\\s*=\\s*[^\\s)]+", QRegularExpression::CaseInsensitiveOption));
            body = body.simplified();
            if (!body.contains(QRegularExpression("\\bIs\\s*=", QRegularExpression::CaseInsensitiveOption))) {
                body.prepend("Is=1e-14 ");
            }
            if (!body.contains(QRegularExpression("\\bN\\s*=", QRegularExpression::CaseInsensitiveOption))) {
                body += " N=1";
            }
            body = body.simplified();
            out = QString(".model %1 D(%2)").arg(diodeMatch.captured(1), body);
            if (warnings && body != originalBody.simplified()) {
                warnings->append(QString("Rewrote LTspice-style diode model parameters for ngspice in: %1").arg(line.trimmed()));
            }
        }
    }

    {
        static const QRegularExpression tranRe(
            "^\\s*\\.tran\\s+(\\S+)\\s+(\\S+)(?:\\s+(\\S+))?(?:\\s+(\\S+))?(.*)$",
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch tranMatch = tranRe.match(out);
        if (tranMatch.hasMatch()) {
            const QString tstep = tranMatch.captured(1).trimmed();
            const QString tstop = tranMatch.captured(2).trimmed();
            QString tstart = tranMatch.captured(3).trimmed();
            QString tmax = tranMatch.captured(4).trimmed();
            QString tail = tranMatch.captured(5).simplified();

            auto promoteModifierToken = [&tail](QString& token) {
                if (token.compare("startup", Qt::CaseInsensitive) == 0 ||
                    token.compare("uic", Qt::CaseInsensitive) == 0) {
                    if (!tail.isEmpty()) tail.prepend(token + " ");
                    else tail = token;
                    token.clear();
                }
            };
            promoteModifierToken(tstart);
            promoteModifierToken(tmax);

            // Strip LTspice-only 'startup'; source ramping is handled separately.
            bool changed = false;
            if (tail.contains("startup", Qt::CaseInsensitive)) {
                tail.remove("startup", Qt::CaseInsensitive);
                tail = tail.trimmed();
                changed = true;
                if (warnings) {
                    warnings->append(QString("Removed LTspice 'startup' keyword from .tran and approximated it by ramping top-level independent sources over the first 20us."));
                }
            }

            if ((tstep == "0" || tstep == "0.0") && !tmax.isEmpty()) {
                out = QString(".tran %1 %2").arg(tmax, tstop);
                if (!tstart.isEmpty()) out += " " + tstart;
                out += " " + tmax; // Re-add tmax as the 4th parameter for ngspice tmax behavior
                if (!tail.isEmpty()) out += " " + tail;
                if (warnings) {
                    warnings->append(QString("Rewrote .tran with zero print step to preserve LTspice tmax behavior for ngspice: %1").arg(line.trimmed()));
                }
            } else if (changed || !tail.isEmpty() || out != tranMatch.captured(0)) {
                // Update the line to reflect stripped startup or other changes
                out = QString(".tran %1 %2").arg(tstep, tstop);
                if (!tstart.isEmpty()) out += " " + tstart;
                if (!tmax.isEmpty()) out += " " + tmax;
                if (!tail.isEmpty()) out += " " + tail;
            }
        }
    }

    if (out.startsWith(".include ", Qt::CaseInsensitive) || out.startsWith(".lib ", Qt::CaseInsensitive) || out.startsWith(".inc ", Qt::CaseInsensitive)) {
        static const QRegularExpression incRe("^(?:\\.(?:include|lib|inc))\\s+(?:\"?)(spice/[^\"]+)(?:\"?)\\s*$", QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch incMatch = incRe.match(out);
        if (incMatch.hasMatch()) {
            QString keyword = out.left(out.indexOf(' '));
            QString oldPath = incMatch.captured(1);
            QString newPath = "sub/" + oldPath.mid(6);
            out = QString("%1 \"%2\"").arg(keyword, newPath);
            if (warnings) {
                warnings->append(QString("Auto-corrected legacy include path from %1 to %2 for backwards compatibility.").arg(oldPath, newPath));
            }
        }
    }

    return out;
}

QStringList collapseSpiceContinuationLines(const QString& text) {
    QStringList collapsed;
    QString current;

    const QStringList lines = text.split('\n');
    for (const QString& rawLine : lines) {
        const QString trimmed = rawLine.trimmed();

        if (trimmed.startsWith('+')) {
            const QString continuation = trimmed.mid(1).trimmed();
            if (current.isEmpty()) {
                current = continuation;
            } else if (!continuation.isEmpty()) {
                if (!current.endsWith(' ')) current += ' ';
                current += continuation;
            }
            continue;
        }

        if (!current.isEmpty()) {
            collapsed.append(current);
        }
        current = rawLine;
    }

    if (!current.isEmpty()) {
        collapsed.append(current);
    }

    return collapsed;
}

bool naturalPinLessThan(const QString& s1, const QString& s2) {
    bool ok1, ok2;
    int n1 = s1.toInt(&ok1);
    int n2 = s2.toInt(&ok2);
    if (ok1 && ok2) return n1 < n2;
    if (ok1) return true;
    if (ok2) return false;
    return s1 < s2;
}

QString fuzzyMatchPin(const QMap<QString, QString>& pins, const QString& subPinName) {
    const QString sub = subPinName.trimmed().toUpper();
    // Try exact match first
    if (pins.contains(sub)) return pins.value(sub);
    
    // Try with underscores removed
    QString simplified = sub;
    simplified.remove('_');
    if (pins.contains(simplified)) return pins.value(simplified);

    // Common Op-Amp patterns
    if (sub.contains("IN") && sub.contains("P")) {
        for (const QString& k : {"+", "IN+", "IN_P", "IP", "VIN+"}) 
            if (pins.contains(k)) return pins.value(k);
    }
    if (sub.contains("IN") && (sub.contains("N") || sub.contains("M"))) {
        for (const QString& k : {"-", "IN-", "IN_N", "IN_M", "IM", "VIN-"}) 
            if (pins.contains(k)) return pins.value(k);
    }
    if (sub.contains("OUT")) {
        for (const QString& k : {"OUT", "O", "VOUT"}) 
            if (pins.contains(k)) return pins.value(k);
    }
    if (sub.contains("VCC") || sub.contains("VDD") || sub.contains("VPP")) {
        for (const QString& k : {"V+", "VCC", "VDD", "VPP", "PVP"}) 
            if (pins.contains(k)) return pins.value(k);
    }
    if (sub.contains("VEE") || sub.contains("VSS") || sub.contains("VNN") || sub.contains("GND")) {
        for (const QString& k : {"V-", "VEE", "VSS", "VNN", "GND", "0"}) 
            if (pins.contains(k)) return pins.value(k);
    }
    
    return QString();
}

QString spicetypeToString(SimComponentType type) {
    switch (type) {
        case SimComponentType::Diode:           return "D";
        case SimComponentType::BJT_NPN:         return "NPN";
        case SimComponentType::BJT_PNP:         return "PNP";
        case SimComponentType::MOSFET_NMOS:     return "NMOS";
        case SimComponentType::MOSFET_PMOS:     return "PMOS";
        case SimComponentType::JFET_NJF:        return "NJF";
        case SimComponentType::JFET_PJF:        return "PJF";
        case SimComponentType::Switch:          return "SW";
        case SimComponentType::CSW:             return "CSW";
        default: return "";
    }
}

QString modelToSpiceLine(const SimModel& model) {
    const QString typeStr = spicetypeToString(model.type);
    if (typeStr.isEmpty()) return QString();

    QSet<QString> allowed;
    switch (model.type) {
        case SimComponentType::Diode:
            allowed = {"IS", "N", "RS", "VJ", "CJO", "M", "TT", "BV", "IBV"};
            break;
        case SimComponentType::BJT_NPN:
        case SimComponentType::BJT_PNP:
            allowed = {"IS", "BF", "BR", "VAF", "VAR", "CJE", "CJC", "TF", "TR", "RB", "RE", "RC"};
            break;
        case SimComponentType::MOSFET_NMOS:
        case SimComponentType::MOSFET_PMOS:
            allowed = {"VTO", "KP", "LAMBDA", "RD", "RS", "RG", "CGSO", "CGDO", "CGBO", "CBD", "CBS", "PB", "GAMMA", "PHI", "LEVEL"};
            break;
        case SimComponentType::JFET_NJF:
        case SimComponentType::JFET_PJF:
            allowed = {"BETA", "VTO", "LAMBDA", "RD", "RS", "CGS", "CGD", "IS", "PB", "FC"};
            break;
        default:
            break;
    }

    QString line = QString(".model %1 %2(").arg(
        QString::fromStdString(model.name), typeStr);
    bool first = true;
    for (const auto& [key, val] : model.params) {
        const QString qkey = QString::fromStdString(key).toUpper();
        if (!allowed.isEmpty() && !allowed.contains(qkey)) continue;
        if (!first) line += " ";
        line += QString("%1=%2").arg(
            qkey,
            QString::number(val, 'g', 12));
        first = false;
    }
    line += ")";
    return line;
}

QString sanitizeModelIncludeForNgspice(const QString& path) {
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) return path;

    const QString ext = fi.suffix().toLower();
    const QSet<QString> supportedExt = {"lib", "inc", "sub", "sp", "cir", "cmp", "mod"};
    if (!supportedExt.contains(ext)) return path;

    QFile in(path);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) return path;

    const QByteArray raw = in.readAll();
    in.close();
    if (raw.isEmpty()) return path;

    QString content = QString::fromUtf8(raw);
    if (content.contains(QChar::ReplacementCharacter)) {
        content = QString::fromLatin1(raw);
    }
    // Strip comment-only lines. This works around ngspice parsing noise seen
    // with some vendor libraries (e.g. LTspice-format comments in large .lib files).
    QStringList outLines;
    outLines.reserve(content.count('\n') + 1);
    const QStringList lines = content.split('\n');
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith('*') || trimmed.startsWith(';') || trimmed.startsWith('$')) continue;

        const QString otaTranslation = buildNgspiceOtaTranslation(line);
        if (!otaTranslation.isEmpty()) {
            outLines.append(otaTranslation);
            continue;
        }

        QString sanitizedLine = line;
        sanitizedLine = rewriteLtspiceBehavioralFunctions(sanitizedLine, nullptr);
        sanitizedLine.replace(QRegularExpression("\\bnoiseless\\b", QRegularExpression::CaseInsensitiveOption), QString());
        sanitizedLine.replace(QRegularExpression("\\s+;.*$"), QString());
        sanitizedLine.replace(QRegularExpression("\\s{2,}"), QStringLiteral(" "));
        outLines.append(sanitizedLine.trimmed());
    }

    const QString sanitized = outLines.join('\n');
    QByteArray key = fi.absoluteFilePath().toUtf8();
    key += QByteArrayLiteral("|sanitize_v3|");
    key += QByteArray::number(fi.size());
    key += QByteArray::number(fi.lastModified().toMSecsSinceEpoch());
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex());

    const QString cacheDirPath = QDir(QDir::tempPath()).filePath("viospice_model_cache");
    QDir cacheDir(cacheDirPath);
    if (!cacheDir.exists()) cacheDir.mkpath(".");

    const QString outPath = cacheDir.filePath(hash + "_" + fi.fileName());
    QFile out(outPath);
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        out.write(sanitized.toUtf8());
        out.close();
        return outPath;
    }
    return path;
}

struct SpiceTokenSplit {
    QString head;
    QString tail;
};

SpiceTokenSplit splitLeadingSpiceToken(const QString& raw) {
    const QString text = raw.trimmed();
    if (text.isEmpty()) return {};

    const QRegularExpression ws("\\s+");
    const QRegularExpressionMatch match = ws.match(text);
    if (!match.hasMatch()) {
        return {text, QString()};
    }

    const int start = match.capturedStart();
    return {text.left(start).trimmed(), text.mid(start).trimmed()};
}

QString pickPowerNetName(const QMap<QString, QString>& pins, const QString& fallbackValue) {
    QString netName = pins.value("1").trimmed();
    if (!netName.isEmpty()) return netName;

    for (auto it = pins.constBegin(); it != pins.constEnd(); ++it) {
        const QString candidate = it.value().trimmed();
        if (!candidate.isEmpty()) return candidate;
    }
    return fallbackValue.trimmed();
}

QString inferPowerVoltage(const QString& netName, const QString& valueText) {
    // Allow explicit numeric overrides such as "12", "3.3", "5V", "12v".
    const QString raw = valueText.trimmed();
    if (!raw.isEmpty()) {
        static const QRegularExpression numWithOptionalV(
            QStringLiteral("^([+-]?\\d*\\.?\\d+)\\s*[vV]?$"));
        const QRegularExpressionMatch m = numWithOptionalV.match(raw);
        if (m.hasMatch()) return m.captured(1);
    }

    const QString upperNet = netName.toUpper();
    const QString upperVal = valueText.toUpper();
    
    // Explicit negative indicators
    if (upperNet.contains("VEE") || upperNet.contains("VSS") || upperNet.contains("V-") ||
        upperVal.contains("VEE") || upperVal.contains("VSS") || upperVal.contains("V-")) {
        return "-5"; // Fallback to a negative value to trigger VEE mapping
    }

    if (upperNet.contains("12V")) return "12";
    if (upperNet.contains("9V")) return "9";
    if (upperNet.contains("5V")) return "5";
    if (upperNet.contains("3.3V") || upperNet.contains("3V3")) return "3.3";
    if (upperNet.contains("1.8V") || upperNet.contains("1V8")) return "1.8";
    if (upperNet.contains("VBAT") || upperNet.contains("BAT")) return "3.7";
    return "5";
}

struct PwlPoint {
    double time = 0.0;
    QString timeText;
    QString value;
};

QString formatPwlNumber(double value);

QString stripOuterBraces(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.size() >= 2 && trimmed.startsWith('{') && trimmed.endsWith('}')) {
        return trimmed.mid(1, trimmed.size() - 2).trimmed();
    }
    return trimmed;
}

QString scaledPwlExpr(const QString& expr, double scale) {
    if (qFuzzyCompare(scale, 1.0)) return QString("(%1)").arg(expr);
    return QString("((%1)*%2)").arg(expr, formatPwlNumber(scale));
}

QString formatPwlTimeText(const PwlPoint& point) {
    return point.timeText.isEmpty() ? formatPwlNumber(point.time) : point.timeText;
}

bool pwlValuesEquivalent(const QString& lhs, const QString& rhs) {
    double lhsNum = 0.0;
    double rhsNum = 0.0;
    if (SimValueParser::parseSpiceNumber(lhs, lhsNum) && SimValueParser::parseSpiceNumber(rhs, rhsNum)) {
        return qFuzzyCompare(1.0 + lhsNum, 1.0 + rhsNum);
    }
    return lhs.trimmed().compare(rhs.trimmed(), Qt::CaseInsensitive) == 0;
}

QString stripOuterQuotes(const QString& text) {
    if (text.size() >= 2 && ((text.startsWith('"') && text.endsWith('"')) || (text.startsWith('\'') && text.endsWith('\'')))) {
        return text.mid(1, text.size() - 2);
    }
    return text;
}

QString formatPwlNumber(double value) {
    return QString::number(value, 'g', 12);
}

QStringList tokenizePwlBody(const QString& text) {
    QStringList tokens;
    QString current;
    int parenDepth = 0;
    int braceDepth = 0;
    bool inQuotes = false;
    QChar quoteChar;

    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (inQuotes) {
            current += ch;
            if (ch == quoteChar) inQuotes = false;
            continue;
        }
        if (ch == '\'' || ch == '"') {
            inQuotes = true;
            quoteChar = ch;
            current += ch;
            continue;
        }
        if (ch == '(') ++parenDepth;
        else if (ch == ')' && parenDepth > 0) --parenDepth;
        else if (ch == '{') ++braceDepth;
        else if (ch == '}' && braceDepth > 0) --braceDepth;

        if ((ch.isSpace() || ch == ',') && parenDepth == 0 && braceDepth == 0) {
            if (!current.trimmed().isEmpty()) tokens.append(current.trimmed());
            current.clear();
            continue;
        }
        current += ch;
    }
    if (!current.trimmed().isEmpty()) tokens.append(current.trimmed());
    return tokens;
}

bool loadPwlPointsFromFile(const QString& fileToken, const QString& projectDir, bool scopeData,
                           QList<PwlPoint>* points, QString* error) {
    if (!points) return false;
    QString path = stripOuterQuotes(fileToken.trimmed());
    QFileInfo fi(path);
    if (fi.isRelative() && !projectDir.isEmpty()) path = QDir(projectDir).filePath(path);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QString("Could not open LTspice PWL file '%1'.").arg(fileToken);
        return false;
    }

    QString allText;
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (!scopeData) {
            const int semicolon = line.indexOf(';');
            const int hash = line.indexOf('#');
            const int star = line.trimmed().startsWith('*') ? 0 : -1;
            int commentPos = -1;
            if (semicolon >= 0) commentPos = semicolon;
            if (hash >= 0 && (commentPos < 0 || hash < commentPos)) commentPos = hash;
            if (star == 0) commentPos = 0;
            if (commentPos == 0) continue;
            if (commentPos > 0) line = line.left(commentPos);
        }
        allText += line;
        allText += ' ';
    }

    const QStringList tokens = tokenizePwlBody(allText);
    if (tokens.size() < 2 || (tokens.size() % 2) != 0) {
        if (error) *error = QString("LTspice PWL file '%1' does not contain time/value pairs.").arg(fileToken);
        return false;
    }

    QMap<double, QList<double>> scopedBuckets;
    for (int i = 0; i < tokens.size(); i += 2) {
        double timeValue = 0.0;
        double yValue = 0.0;
        if (!SimValueParser::parseSpiceNumber(tokens.at(i), timeValue) || !SimValueParser::parseSpiceNumber(tokens.at(i + 1), yValue)) {
            if (error) *error = QString("LTspice PWL file '%1' contains non-numeric data unsupported by VioSpice.").arg(fileToken);
            return false;
        }
        if (scopeData) {
            if (timeValue < 0.0) continue;
            scopedBuckets[timeValue].append(yValue);
        } else {
            points->append({timeValue, formatPwlNumber(timeValue), tokens.at(i + 1)});
        }
    }

    if (scopeData) {
        for (auto it = scopedBuckets.cbegin(); it != scopedBuckets.cend(); ++it) {
            double avg = 0.0;
            for (double v : it.value()) avg += v;
            avg /= static_cast<double>(it.value().size());
            points->append({it.key(), formatPwlNumber(it.key()), formatPwlNumber(avg)});
        }
    }

    return !points->isEmpty();
}

bool loadCurrentTablePairsFromFile(const QString& fileToken, const QString& projectDir, QStringList* pairs, QString* error) {
    if (!pairs) return false;
    QString path = stripOuterQuotes(fileToken.trimmed());
    QFileInfo fi(path);
    if (fi.isRelative() && !projectDir.isEmpty()) path = QDir(projectDir).filePath(path);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QString("Could not open LTspice current-source table file '%1'.").arg(fileToken);
        return false;
    }

    QString allText;
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine();
        const int semicolon = line.indexOf(';');
        const int hash = line.indexOf('#');
        const int star = line.trimmed().startsWith('*') ? 0 : -1;
        int commentPos = -1;
        if (semicolon >= 0) commentPos = semicolon;
        if (hash >= 0 && (commentPos < 0 || hash < commentPos)) commentPos = hash;
        if (star == 0) commentPos = 0;
        if (commentPos == 0) continue;
        if (commentPos > 0) line = line.left(commentPos);
        allText += line;
        allText += ' ';
    }

    *pairs = tokenizePwlBody(allText);
    if (pairs->size() < 2 || (pairs->size() % 2) != 0) {
        if (error) *error = QString("LTspice current-source table file '%1' does not contain voltage/current pairs.").arg(fileToken);
        return false;
    }
    return true;
}

QString buildStepApproxPwl(const QString& initialValue, const QStringList& stepValues) {
    QStringList tokens;
    const QString settleTime = "1m";
    const QString rampTime = "10u";

    QString currentTime = "0";
    QString activeValue = initialValue.trimmed();
    tokens << currentTime << activeValue;
    for (const QString& nextValue : stepValues) {
        tokens << QString("{%1}").arg(currentTime.isEmpty() ? settleTime : currentTime + "+" + settleTime) << activeValue;
        currentTime = QString("{%1+%2+%3}").arg(currentTime, settleTime, rampTime);
        activeValue = nextValue.trimmed();
        tokens << currentTime << activeValue;
    }
    return QString("PWL(%1)").arg(tokens.join(' '));
}

bool rewriteLtspiceCurrentSourceSpecial(const QString& ref, const QString& nplus, const QString& nminus,
                                        const QString& value, const QString& projectDir,
                                        QString* replacement, QStringList* warnings) {
    if (!replacement) return false;
    const QString trimmed = value.trimmed();

    {
        static const QRegularExpression resistiveRe(R"(^R\s*=\s*(.+)$)", QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch m = resistiveRe.match(trimmed);
        if (m.hasMatch()) {
            const QString rval = m.captured(1).trimmed();
            *replacement = QString("R__ILOAD_%1 %2 %3 %4").arg(ref, nplus, nminus, rval);
            if (warnings) warnings->append(QString("Rewrote LTspice current-source R= load on %1 into an equivalent resistor for ngspice.").arg(ref));
            return true;
        }
    }

    {
        static const QRegularExpression tableRe(R"(^(?:tbl|table)\s*=\s*(.+)$)", QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch m = tableRe.match(trimmed);
        if (m.hasMatch()) {
            QString spec = m.captured(1).trimmed();
            QStringList args;
            if (spec.startsWith('(') && spec.endsWith(')')) {
                args = splitTopLevelSpiceArgs(spec.mid(1, spec.size() - 2));
            } else if ((spec.startsWith('"') && spec.endsWith('"')) || (!spec.contains(',') && !spec.contains('(') && !spec.contains('{'))) {
                QString error;
                if (!loadCurrentTablePairsFromFile(spec, projectDir, &args, &error)) {
                    if (warnings && !error.isEmpty()) warnings->append(error);
                    return false;
                }
            } else if (spec.startsWith('{') && spec.endsWith('}')) {
                if (warnings) warnings->append(QString("LTspice current-source table filename parameter on %1 is not yet supported by VioSpice.").arg(ref));
                return false;
            }

            QString error;
            const QString expr = buildCurrentTableExpr(QString("V(%1,%2)").arg(nplus, nminus), args, &error);
            if (expr.isEmpty()) {
                if (warnings && !error.isEmpty()) warnings->append(error);
                return false;
            }
            QString bref = ref;
            if (!bref.startsWith('B', Qt::CaseInsensitive)) bref = "B__ITBL_" + ref;
            *replacement = QString("%1 %2 %3 I={%4}").arg(bref, nplus, nminus, expr);
            if (warnings) warnings->append(QString("Rewrote LTspice current-source tbl/table on %1 into a behavioral current source for ngspice.").arg(ref));
            return true;
        }
    }

    {
        static const QRegularExpression stepRe(R"(^(.+?)\s+step\s*\((.*)\)\s*(load)?\s*$)", QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch m = stepRe.match(trimmed);
        if (m.hasMatch()) {
            const QString baseValue = m.captured(1).trimmed();
            const QStringList stepValues = splitTopLevelSpiceArgs(m.captured(2));
            if (!stepValues.isEmpty()) {
                *replacement = QString("%1 %2 %3 %4").arg(ref, nplus, nminus, buildStepApproxPwl(baseValue, stepValues));
                if (warnings) warnings->append(QString("Approximated LTspice current-source step(...) on %1 with a heuristic PWL load sequence; LTspice steady-state step timing is not fully reproduced.").arg(ref));
                return true;
            }
        }
    }

    return false;
}

bool appendPwlPointList(const QString& listText, double timeScale, double valueScale, double baseTime, QList<PwlPoint>* points,
                        QString* error) {
    if (!points) return false;
    const QStringList items = splitTopLevelSpiceArgs(listText);
    if (items.size() < 2 || (items.size() % 2) != 0) {
        if (error) *error = "LTspice PWL point list must contain time/value pairs.";
        return false;
    }

    double previousTime = points->isEmpty() ? baseTime : points->last().time;
    QString previousTimeText = points->isEmpty() ? formatPwlNumber(baseTime) : formatPwlTimeText(points->last());
    for (int i = 0; i < items.size(); i += 2) {
        const QString timeToken = items.at(i).trimmed();
        const QString valueToken = items.at(i + 1).trimmed();
        double parsedTime = 0.0;
        double parsedValue = 0.0;
        const QString normalizedTime = timeToken.startsWith('+') ? timeToken.mid(1).trimmed() : timeToken;
        const bool timeIsNumeric = SimValueParser::parseSpiceNumber(normalizedTime, parsedTime);
        if (!SimValueParser::parseSpiceNumber(valueToken, parsedValue)) {
            if (error) *error = QString("Unsupported LTspice PWL expression '%1, %2'; VioSpice currently requires numeric values.").arg(timeToken, valueToken);
            return false;
        }

        if (timeIsNumeric) {
            double finalTime = timeToken.startsWith('+') ? previousTime + (parsedTime * timeScale) : baseTime + (parsedTime * timeScale);
            previousTime = finalTime;
            previousTimeText = formatPwlNumber(finalTime);
            points->append({finalTime, previousTimeText, formatPwlNumber(parsedValue * valueScale)});
            continue;
        }

        if (!(normalizedTime.startsWith('{') && normalizedTime.endsWith('}'))) {
            if (error) *error = QString("Unsupported LTspice PWL time expression '%1'; VioSpice currently supports numeric times or brace expressions.").arg(timeToken);
            return false;
        }

        const QString expr = stripOuterBraces(normalizedTime);
        QString finalTimeText;
        if (timeToken.startsWith('+')) {
            finalTimeText = QString("{%1+%2}").arg(previousTimeText, scaledPwlExpr(expr, timeScale));
        } else {
            const QString scaledExpr = scaledPwlExpr(expr, timeScale);
            finalTimeText = qFuzzyIsNull(baseTime) ? QString("{%1}").arg(scaledExpr)
                                                  : QString("{%1+%2}").arg(formatPwlNumber(baseTime), scaledExpr);
        }
        previousTime = std::numeric_limits<double>::quiet_NaN();
        previousTimeText = finalTimeText;
        points->append({previousTime, finalTimeText, formatPwlNumber(parsedValue * valueScale)});
    }
    return true;
}

bool appendExpandedPwlSpecs(const QStringList& tokens, int* index, double timeScale, double valueScale, double baseTime,
                            QList<PwlPoint>* points, QStringList* warnings, const QString& projectDir, QString* error) {
    if (!index || !points) return false;

    auto isPwlKeyword = [](const QString& text) {
        return text.compare("ENDREPEAT", Qt::CaseInsensitive) == 0 ||
               text.compare("REPEAT", Qt::CaseInsensitive) == 0 ||
               text.compare("FOR", Qt::CaseInsensitive) == 0 ||
               text.compare("FOREVER", Qt::CaseInsensitive) == 0 ||
               text.startsWith("FILE=", Qt::CaseInsensitive) ||
               text.startsWith("SCOPEDATA=", Qt::CaseInsensitive);
    };

    while (*index < tokens.size()) {
        const QString token = tokens.at(*index);
        if (token.compare("ENDREPEAT", Qt::CaseInsensitive) == 0) {
            ++(*index);
            return true;
        }
        if (token.startsWith("(") && token.endsWith(")")) {
            if (!appendPwlPointList(token.mid(1, token.size() - 2), timeScale, valueScale, baseTime, points, error)) return false;
            ++(*index);
            continue;
        }
        if (token.startsWith("FILE=", Qt::CaseInsensitive) || token.startsWith("SCOPEDATA=", Qt::CaseInsensitive)) {
            QList<PwlPoint> filePoints;
            const bool scopeData = token.startsWith("SCOPEDATA=", Qt::CaseInsensitive);
            const QString fileToken = token.mid(scopeData ? 10 : 5);
            if (!loadPwlPointsFromFile(fileToken, projectDir, scopeData, &filePoints, error)) return false;
            if (filePoints.isEmpty()) {
                if (error) *error = QString("LTspice PWL %1 file '%2' contained no usable points.").arg(scopeData ? "SCOPEDATA" : "FILE", fileToken);
                return false;
            }
            const double origin = points->isEmpty() ? baseTime : points->last().time;
            for (const PwlPoint& point : filePoints) {
                double scaledValue = 0.0;
                if (!SimValueParser::parseSpiceNumber(point.value, scaledValue)) {
                    if (error) *error = QString("LTspice PWL file '%1' contains unsupported non-numeric values.").arg(fileToken);
                    return false;
                }
                const double finalTime = origin + (point.time * timeScale);
                points->append({finalTime, formatPwlNumber(finalTime), formatPwlNumber(scaledValue * valueScale)});
            }
            ++(*index);
            continue;
        }
        if (token.compare("REPEAT", Qt::CaseInsensitive) == 0) {
            ++(*index);
            if (*index >= tokens.size()) {
                if (error) *error = "Incomplete LTspice PWL REPEAT block.";
                return false;
            }
            bool repeatForever = false;
            int repeatCount = 0;
            if (tokens.at(*index).compare("FOREVER", Qt::CaseInsensitive) == 0) {
                repeatForever = true;
                ++(*index);
            } else {
                if (tokens.at(*index).compare("FOR", Qt::CaseInsensitive) == 0) ++(*index);
                if (*index >= tokens.size()) {
                    if (error) *error = "Incomplete LTspice PWL REPEAT FOR count.";
                    return false;
                }
                double parsedCount = 0.0;
                if (!SimValueParser::parseSpiceNumber(tokens.at(*index), parsedCount) || parsedCount < 0.0) {
                    if (error) *error = QString("Unsupported LTspice PWL repeat count '%1'.").arg(tokens.at(*index));
                    return false;
                }
                repeatCount = static_cast<int>(std::llround(parsedCount));
                ++(*index);
            }

            QList<PwlPoint> repeatedBody;
            int nestedIndex = *index;
            if (!appendExpandedPwlSpecs(tokens, &nestedIndex, timeScale, valueScale, 0.0, &repeatedBody, warnings, projectDir, error)) return false;
            *index = nestedIndex;
            if (repeatedBody.isEmpty()) continue;

            if (repeatForever) {
                if (warnings) warnings->append("LTspice PWL REPEAT FOREVER is not fully supported by VioSpice; keeping a single waveform period.");
                repeatCount = 1;
            }
            if (repeatCount > 1 && !repeatedBody.isEmpty() && qFuzzyIsNull(repeatedBody.first().time) &&
                !pwlValuesEquivalent(repeatedBody.first().value, repeatedBody.last().value)) {
                if (error) *error = "Ill-formed LTspice PWL REPEAT block: first repeated time is zero but first and last values differ.";
                return false;
            }
            const double span = repeatedBody.last().time;
            for (int rep = 0; rep < repeatCount; ++rep) {
                const double origin = points->isEmpty() ? baseTime : points->last().time;
                for (const PwlPoint& point : repeatedBody) {
                    points->append({origin + point.time, formatPwlNumber(origin + point.time), point.value});
                }
                if ((!std::isfinite(span) || span <= 0.0) && rep + 1 < repeatCount) {
                    if (error) *error = "Ill-formed LTspice PWL REPEAT block with zero span.";
                    return false;
                }
            }
            continue;
        }

        if (!isPwlKeyword(token)) {
            QStringList pointTokens;
            int pointIndex = *index;
            while (pointIndex < tokens.size() && !isPwlKeyword(tokens.at(pointIndex))) {
                pointTokens.append(tokens.at(pointIndex));
                ++pointIndex;
            }
            if (!appendPwlPointList(pointTokens.join(','), timeScale, valueScale, baseTime, points, error)) return false;
            *index = pointIndex;
            continue;
        }

        if (error) *error = QString("Unsupported LTspice PWL token '%1'.").arg(token);
        return false;
    }
    return true;
}

QString inlinePwlFileIfNeeded(const QString& value, const QString& projectDir, QStringList* warnings) {
    const QString v = value.trimmed();
    if (!v.contains("PWL", Qt::CaseInsensitive)) return value;

    QString body;
    QString tail;

    // Regex to match PWL(...) and capture the inside, also allowing trailing params
    QRegularExpression re(R"(^PWL\s*\((.*)\)\s*(.*)$)", QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch fullMatch = re.match(v);
    if (fullMatch.hasMatch()) {
        body = fullMatch.captured(1).trimmed();
        tail = fullMatch.captured(2).trimmed();
    } else {
        // Try without parentheses: PWL 0 0 1 1 ...
        QRegularExpression re2(R"(^PWL\s+(.*)$)", QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
        const QRegularExpressionMatch match2 = re2.match(v);
        if (!match2.hasMatch()) return value;
        body = match2.captured(1).trimmed();
    }

    // Check for FILE="..." syntax
    QRegularExpression fileRe(R"(FILE\s*=\s*["']?([^"']+)["']?)", QRegularExpression::CaseInsensitiveOption);
    auto fileMatch = fileRe.match(body);
    if (fileMatch.hasMatch()) {
        QString path = fileMatch.captured(1);
        // Use the new native VioMATRIXC 'pwlfile' parameter
        QString result = QString("pwlfile=\"%1\"").arg(path);
        if (!tail.isEmpty()) result += " " + tail;
        return result;
    }

    QStringList tokens = tokenizePwlBody(body);
    if (tokens.isEmpty()) return value;

    double timeScale = 1.0;
    double valueScale = 1.0;
    while (!tokens.isEmpty()) {
        const QString token = tokens.first();
        if (token.startsWith("TIME_SCALE_FACTOR=", Qt::CaseInsensitive)) {
            if (!SimValueParser::parseSpiceNumber(token.mid(18), timeScale)) return value;
            tokens.removeFirst();
            continue;
        }
        if (token.startsWith("VALUE_SCALE_FACTOR=", Qt::CaseInsensitive)) {
            if (!SimValueParser::parseSpiceNumber(token.mid(19), valueScale)) return value;
            tokens.removeFirst();
            continue;
        }
        break;
    }

    QList<PwlPoint> points;
    int index = 0;
    QString error;
    if (!appendExpandedPwlSpecs(tokens, &index, timeScale, valueScale, 0.0, &points, warnings, projectDir, &error) || index != tokens.size()) {
        if (warnings && !error.isEmpty()) warnings->append(QString("Could not fully translate LTspice PWL syntax '%1': %2").arg(v, error));
        return value;
    }
    if (points.isEmpty()) return value;

    QStringList flatTokens;
    for (const PwlPoint& point : points) {
        flatTokens << formatPwlTimeText(point) << point.value;
    }
    
    QString result = QString("PWL(%1)").arg(flatTokens.join(' '));
    if (!tail.isEmpty()) result += " " + tail;
    return result;
}

QString formatPwlValueForNetlist(const QString& value, int maxLen = 120) {
    const QString v = value.trimmed();
    
    // If it contains FILE or WAVEFILE, we don't want to mess with wrapping 
    if (v.contains("FILE", Qt::CaseInsensitive) || v.contains("WAVEFILE", Qt::CaseInsensitive)) {
        return v;
    }

    if (!v.startsWith("PWL", Qt::CaseInsensitive)) return value;

    int closeIdx = v.lastIndexOf(')');
    if (closeIdx < 0) return value;

    QString tail = v.mid(closeIdx + 1).trimmed();
    QString inside = v.left(closeIdx + 1);
    int openIdx = inside.indexOf('(');
    if (openIdx < 0) return value;

    const QString head = "PWL(";
    const QString body = inside.mid(openIdx + 1, inside.length() - openIdx - 2);

    // Split body into tokens and filter out existing continuation characters
    QStringList rawTokens = body.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    QStringList tokens;
    for (const QString& t : rawTokens) {
        if (t != "+") tokens << t;
    }
    
    if (tokens.isEmpty()) return value;

    QString result = head;
    
    // Try to keep time/value pairs on the same line
    for (int i = 0; i < tokens.size(); i += 2) {
        QString pair = tokens[i];
        if (i + 1 < tokens.size()) pair += " " + tokens[i+1];
        
        const int extra = pair.length() + 1;
        // Check if adding this pair would exceed maxLen
        // We look at the length of the LAST line of 'result'
        int lastLineLen = result.length() - result.lastIndexOf('\n') - 1;
        if (lastLineLen > 0 && lastLineLen + extra > maxLen) {
            result += "\n+ " + pair;
        } else {
            if (!result.endsWith("(") && !result.endsWith("+ ")) result += " ";
            result += pair;
        }
    }

    result += ")";
    if (!tail.isEmpty()) result += " " + tail;

    return result;
}

QString currentSaveVectorForRef(const QString& spiceRef) {
    const QString ref = spiceRef.trimmed();
    if (ref.isEmpty()) return QString();

    const QChar prefix = ref.at(0).toUpper();
    switch (prefix.unicode()) {
    case 'R':
    case 'C':
    case 'L':
    case 'D':
    case 'B':
        return QString("@%1[i]").arg(ref);
    case 'Q':
        return QString("@%1[ic]").arg(ref);
    case 'M':
    case 'J':
    case 'Z':
        return QString("@%1[id]").arg(ref);
    default:
        return QString();
    }
}

struct VoltageParasitics {
    QString value;
    QString rser;
    QString cpar;
};

static VoltageParasitics stripVoltageParasitics(const QString& value) {
    VoltageParasitics out{value, "", ""};
    QRegularExpression rserRe("\\bRser\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpression cparRe("\\bCpar\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);

    auto rserMatch = rserRe.match(out.value);
    if (rserMatch.hasMatch()) {
        out.rser = rserMatch.captured(1).trimmed();
        out.value.remove(rserRe);
    }
    auto cparMatch = cparRe.match(out.value);
    if (cparMatch.hasMatch()) {
        out.cpar = cparMatch.captured(1).trimmed();
        out.value.remove(cparRe);
    }
    out.value = out.value.trimmed();
    return out;
}

QString resolveModelPath(const QString& modelPath, const QString& projectDir) {
    if (modelPath.trimmed().isEmpty()) return QString();
    QFileInfo fi(modelPath);
    if (fi.isAbsolute()) return fi.absoluteFilePath();

    const QString source = modelPath;
    if (!projectDir.isEmpty()) {
        const QString candidate = QDir(projectDir).filePath(source);
        if (QFileInfo::exists(candidate)) return candidate;
    }

    const QStringList roots = ConfigManager::instance().libraryRoots();
    for (const QString& root : roots) {
        if (root.trimmed().isEmpty()) continue;
        const QString candidate = QDir(root).filePath(source);
        if (QFileInfo::exists(candidate)) return candidate;
    }

    // Fallback: default Viospice subcircuit library
    {
        const QString libRoot = QDir::homePath() + "/ViospiceLib";
        
        // If path starts with "sub/" try it relative to ViospiceLib root (correct path)
        if (source.startsWith("sub/", Qt::CaseInsensitive)) {
            const QString candidate = QDir(libRoot).filePath(source);
            if (QFileInfo::exists(candidate)) return candidate;
            // Extension fallback: .lib <-> .sub
            QFileInfo fic(candidate);
            const QString altExt = fic.suffix().toLower() == "lib" ? ".sub" : ".lib";
            const QString altCandidate = fic.dir().filePath(fic.completeBaseName() + altExt);
            if (QFileInfo::exists(altCandidate)) return altCandidate;
        } else {
            // Try inside sub/ subdirectory
            const QString candidate = QDir(libRoot + "/sub").filePath(source);
            if (QFileInfo::exists(candidate)) return candidate;
            // Extension fallback inside sub/
            QFileInfo fic(candidate);
            const QString altExt = fic.suffix().toLower() == "lib" ? ".sub" : ".lib";
            const QString altCandidate = fic.dir().filePath(fic.completeBaseName() + altExt);
            if (QFileInfo::exists(altCandidate)) return altCandidate;
        }
        
        // Backwards compatibility for paths saved as spice/filename
        if (source.startsWith("spice/")) {
            const QString compatSource = source.mid(6);
            const QString candidate = QDir(libRoot + "/sub").filePath(compatSource);
            if (QFileInfo::exists(candidate)) return candidate;
            // Extension fallback
            QFileInfo fic(candidate);
            const QString altExt = fic.suffix().toLower() == "lib" ? ".sub" : ".lib";
            const QString altCandidate = fic.dir().filePath(fic.completeBaseName() + altExt);
            if (QFileInfo::exists(altCandidate)) return altCandidate;
        }
    }

    return modelPath;
}

QString normalizeIncludePathForNetlist(const QString& includePath, const QString& projectDir) {
    QString resolvedPath = QDir::fromNativeSeparators(includePath.trimmed());
    if (resolvedPath.isEmpty()) return resolvedPath;

    QFileInfo fi(resolvedPath);
    if (!fi.isAbsolute()) {
        // Prefer project-relative resolution first.
        const QString projectCandidate = QDir(projectDir).absoluteFilePath(resolvedPath);
        if (QFileInfo::exists(projectCandidate)) {
            resolvedPath = QDir::cleanPath(projectCandidate);
        } else {
            // Fall back to known library roots.
            const QStringList roots = ConfigManager::instance().libraryRoots();
            bool found = false;
            for (const QString& root : roots) {
                if (root.isEmpty()) continue;

                // If path already starts with "sub/", resolve directly against the root
                // to avoid a double "sub/sub/" when the root itself ends with /sub.
                if (resolvedPath.startsWith("sub/", Qt::CaseInsensitive)) {
                    QString candidate = QDir(root).absoluteFilePath(resolvedPath);
                    if (QFileInfo::exists(candidate)) {
                        resolvedPath = QDir::cleanPath(candidate);
                        found = true;
                        break;
                    }
                    // Extension fallback: .lib <-> .sub
                    QFileInfo fic(candidate);
                    const QString altExt = fic.suffix().toLower() == "lib" ? ".sub" : ".lib";
                    candidate = fic.dir().filePath(fic.completeBaseName() + altExt);
                    if (QFileInfo::exists(candidate)) {
                        resolvedPath = QDir::cleanPath(candidate);
                        found = true;
                        break;
                    }
                } else {
                    QString candidate = QDir(root).absoluteFilePath(resolvedPath);
                    if (QFileInfo::exists(candidate)) {
                        resolvedPath = QDir::cleanPath(candidate);
                        found = true;
                        break;
                    }
                    // Extension fallback
                    QFileInfo fic(candidate);
                    const QString altExt = fic.suffix().toLower() == "lib" ? ".sub" : ".lib";
                    candidate = fic.dir().filePath(fic.completeBaseName() + altExt);
                    if (QFileInfo::exists(candidate)) {
                        resolvedPath = QDir::cleanPath(candidate);
                        found = true;
                        break;
                    }
                }

                // Backwards compatibility: spice/X -> sub/X
                if (resolvedPath.startsWith("spice/")) {
                    const QString compat = "sub/" + resolvedPath.mid(6);
                    QString candidate = QDir(root).absoluteFilePath(compat);
                    if (QFileInfo::exists(candidate)) {
                        resolvedPath = QDir::cleanPath(candidate);
                        found = true;
                        break;
                    }
                    QFileInfo fic(candidate);
                    const QString altExt = fic.suffix().toLower() == "lib" ? ".sub" : ".lib";
                    candidate = fic.dir().filePath(fic.completeBaseName() + altExt);
                    if (QFileInfo::exists(candidate)) {
                        resolvedPath = QDir::cleanPath(candidate);
                        found = true;
                        break;
                    }
                }
            }
            Q_UNUSED(found);
        }
    } else if (QFileInfo::exists(resolvedPath)) {
        resolvedPath = QFileInfo(resolvedPath).absoluteFilePath();
    }

    return QDir::fromNativeSeparators(QDir::cleanPath(resolvedPath));
}

QString sanitizeDirectiveName(const QString& raw) {
    QString s = raw;
    s.replace(QRegularExpression("[^A-Za-z0-9_]"), "_");
    s.replace(QRegularExpression("_+"), "_");
    s.remove(QRegularExpression("^_+"));
    s.remove(QRegularExpression("_+$"));
    return s.isEmpty() ? QString("m") : s.left(40);
}

QString normalizeLtspiceMeasDirective(const QString& cmd, QStringList* warnings = nullptr) {
    QString out = cmd;

    if (!out.startsWith(".meas", Qt::CaseInsensitive)) return out;

    if (out.contains("I(", Qt::CaseInsensitive)) {
        if (warnings) {
            warnings->append(QString("LTspice-style .meas current reference detected: %1").arg(cmd.trimmed()));
            warnings->append(QString("Consider measuring source current via I(Vsense) or converting resistor current measurements manually for ngspice."));
        }
    }

    if (out.contains(" PARAM ", Qt::CaseInsensitive)) {
        if (warnings) {
            warnings->append(QString(".meas PARAM detected and passed through unchanged: %1").arg(cmd.trimmed()));
        }
    }

    if (out.contains(" FIND ", Qt::CaseInsensitive) && out.contains(" AT=", Qt::CaseInsensitive)) {
        if (warnings) {
            warnings->append(QString(".meas FIND ... AT= detected; verify LTspice/ngspice syntax compatibility: %1").arg(cmd.trimmed()));
        }
    }

    if (out.contains(QRegularExpression("\\bDERIV\\b", QRegularExpression::CaseInsensitiveOption))) {
        if (warnings) {
            warnings->append(QString(".meas DERIV detected; verify LTspice/ngspice derivative measurement syntax compatibility: %1").arg(cmd.trimmed()));
        }
    }

    if (out.contains(QRegularExpression("\\bTRIG\\b", QRegularExpression::CaseInsensitiveOption)) ||
        out.contains(QRegularExpression("\\bTARG\\b", QRegularExpression::CaseInsensitiveOption))) {
        if (warnings) {
            warnings->append(QString(".meas TRIG/TARG interval form detected; verify LTspice/ngspice compatibility: %1").arg(cmd.trimmed()));
        }
    }

    if (out.contains(QRegularExpression("\\b(RISE|FALL|CROSS)\\s*=\\s*(LAST|\\d+)", QRegularExpression::CaseInsensitiveOption))) {
        if (warnings) {
            warnings->append(QString(".meas RISE/FALL/CROSS qualifier detected; verify LTspice/ngspice event counting compatibility: %1").arg(cmd.trimmed()));
        }
    }

    if (out.contains(QRegularExpression("\\b(AVG|MAX|MIN|PP|RMS|INTEG)\\b", QRegularExpression::CaseInsensitiveOption))) {
        if (warnings) {
            warnings->append(QString(".meas interval reduction keyword detected and passed through unchanged: %1").arg(cmd.trimmed()));
        }
    }

    return out;
}

QString normalizeMeanDirective(const QString& cmd) {
    static const QRegularExpression re(
        "^\\s*\\.mean\\s+(?:(avg|max|min|rms)\\s+)?([^\\s]+)(?:\\s+from\\s*=\\s*([^\\s]+))?(?:\\s+to\\s*=\\s*([^\\s]+))?\\s*$",
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(cmd.trimmed());
    if (!m.hasMatch()) return cmd;

    const QString mode = m.captured(1).isEmpty() ? QString("avg") : m.captured(1).toLower();
    const QString signal = m.captured(2).trimmed();
    const QString from = m.captured(3).trimmed();
    const QString to = m.captured(4).trimmed();
    if (signal.isEmpty()) return cmd;

    const QString name = QString("mean_%1_%2_%3")
        .arg(mode, sanitizeDirectiveName(signal))
        .arg(QString::number(qHash(cmd.toLower()), 16));

    QString out = QString(".meas tran %1 %2 %3").arg(name, mode, signal);
    if (!from.isEmpty()) out += QString(" from=%1").arg(from);
    if (!to.isEmpty()) out += QString(" to=%1").arg(to);
    return out;
}

UserSpiceContentSummary summarizeUserSpiceText(const QString& text, const QString& projectDir) {
    UserSpiceContentSummary summary;

    static const QRegularExpression includeDirectiveRe(
        "^\\s*\\.(lib|inc|include)\\s+(?:\"([^\"]+)\"|(\\S+))",
        QRegularExpression::CaseInsensitiveOption);
    static const QSet<QString> analysisCards = {
        ".tran", ".ac", ".op", ".dc", ".noise", ".four", ".tf",
        ".disto", ".meas", ".step", ".sens"
    };

    const QStringList lines = collapseSpiceContinuationLines(text);
    QSet<QString> analysisSeen;
    QMap<QString, int> modelSeen;
    QMap<QString, int> refSeen;
    QStringList subcktStack;
    int lineNo = 0;
    for (const QString& rawLine : lines) {
        ++lineNo;
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith('*') || line.startsWith(';') || line.startsWith('#')) continue;

        if (line.startsWith('.')) {
            QString card;
            int firstSpace = line.indexOf(' ');
            if (firstSpace > 0) card = line.left(firstSpace).trimmed().toLower();
            else card = line.trimmed().toLower();

            if (analysisCards.contains(card)) {
                summary.hasExplicitAnalysisCard = true;
                if (analysisSeen.contains(card)) {
                    summary.warnings.append(QString("Duplicate analysis card %1 in directive block (line %2).")
                        .arg(card, QString::number(lineNo)));
                } else {
                    analysisSeen.insert(card);
                }
            }

            if (card == ".save") {
                summary.hasExplicitSaveDirective = true;
            }

            if (card == ".net") {
                summary.hasNetDirective = true;
            }

            if (card == ".tran" && line.contains("startup", Qt::CaseInsensitive)) {
                summary.hasLtspiceStartup = true;
            }

            if (card == ".subckt") {
                const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                if (parts.size() >= 2) {
                    subcktStack.append(parts.at(1));
                }
            } else if (card == ".ends") {
                const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                if (subcktStack.isEmpty()) {
                    summary.warnings.append(QString(".ends has no matching .subckt (line %1).").arg(lineNo));
                } else {
                    const QString openName = subcktStack.takeLast();
                    if (parts.size() >= 2 && parts.at(1).compare(openName, Qt::CaseInsensitive) != 0) {
                        summary.warnings.append(QString(".ends %1 does not match open .subckt %2 (line %3).").arg(parts.at(1), openName, QString::number(lineNo)));
                    }
                }
            }

            const QRegularExpressionMatch includeMatch = includeDirectiveRe.match(line);
            if (includeMatch.hasMatch()) {
                const QString rawPath = includeMatch.captured(2).isEmpty()
                    ? includeMatch.captured(3)
                    : includeMatch.captured(2);
                const QString normalized = normalizeIncludePathForNetlist(rawPath, projectDir);
                if (!normalized.isEmpty()) {
                    summary.declaredModelFiles.insert(normalized);
                }
            }

            if (card == ".model") {
                QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                if (parts.size() >= 2) {
                    const QString modelName = parts[1].toLower();
                    if (modelSeen.contains(modelName)) {
                        summary.warnings.append(QString("Duplicate .model %1 in directive block (lines %2 and %3).").arg(parts[1]).arg(modelSeen.value(modelName)).arg(lineNo));
                    } else {
                        modelSeen.insert(modelName, lineNo);
                    }
                    summary.declaredModelNames.insert(modelName);
                }

                if (line.contains(" D(", Qt::CaseInsensitive) &&
                    (line.contains("Ron=", Qt::CaseInsensitive) || line.contains("Roff=", Qt::CaseInsensitive) || line.contains("Vfwd=", Qt::CaseInsensitive))) {
                    summary.warnings.append(QString("LTspice-style diode model parameters detected in line %1; ngspice may reject Ron/Roff/Vfwd on .model D.").arg(lineNo));
                }
            }

            if (card == ".meas" && line.contains("I(", Qt::CaseInsensitive)) {
                summary.warnings.append(QString("Measurement current expression in line %1 may be LTspice-specific; ngspice is less reliable with I(R...) style expressions.").arg(lineNo));
            }

            if ((card == ".meas" || card == ".func" || card == ".param") && line.contains("table(", Qt::CaseInsensitive)) {
                summary.warnings.append(QString("table(...) detected in line %1; VioSpice will approximate inline point-pair forms for ngspice, but file/include-style forms may still differ.").arg(lineNo));
            }

            if (card == ".func") {
                summary.warnings.append(QString("LTspice .func detected in line %1; user-defined functions may rely on LTspice dynamic scoping, so verify ngspice compatibility when referenced inside subcircuits or with local .param overrides.").arg(lineNo));
            }

            if (card == ".step") {
                summary.warnings.append(QString("LTspice .step detected in line %1; this ngspice configuration reports .step as unimplemented, so VioSpice will omit it from the active netlist.").arg(lineNo));
            }

            if (card == ".four") {
                summary.warnings.append(QString("LTspice .four detected in line %1; verify Fourier-analysis compatibility and output behavior in ngspice.").arg(lineNo));
            }

            if (card == ".wave") {
                summary.warnings.append(QString("LTspice .wave detected in line %1; ngspice does not support LTspice WAV export directives.").arg(lineNo));
            }

            if ((card == ".param" || card == ".func") && line.contains("file=", Qt::CaseInsensitive)) {
                summary.warnings.append(QString("LTspice file= syntax detected in line %1; verify ngspice compatibility for file-driven expressions or sweeps.").arg(lineNo));
            }

            continue;
        }

        summary.hasElementCards = true;
        const bool emulateStartupOnLine = summary.hasLtspiceStartup && subcktStack.isEmpty();
        const QString rewrittenLine = rewriteLtspiceDirectiveLine(line, &summary.warnings, emulateStartupOnLine, projectDir);
        if (rewrittenLine.contains("if(", Qt::CaseInsensitive)) {
            summary.warnings.append(QString("LTspice-style if(...) expression remains in line %1 and may fail in ngspice.").arg(lineNo));
        }
        if (line.contains("table(", Qt::CaseInsensitive)) {
            summary.warnings.append(QString("table(...) detected in line %1; VioSpice will approximate inline point-pair forms for ngspice, but file/include-style forms may still differ.").arg(lineNo));
        }
        if (line.contains("wavefile=", Qt::CaseInsensitive)) {
            summary.warnings.append(QString("LTspice wavefile= source detected in line %1; ngspice compatibility for WAV-backed sources is not implemented in VioSpice.").arg(lineNo));
        }
        if (line.contains("chan=", Qt::CaseInsensitive) && line.contains("wavefile=", Qt::CaseInsensitive)) {
            summary.warnings.append(QString("LTspice chan= option for wavefile-backed sources detected in line %1; verify channel-selection compatibility manually.").arg(lineNo));
        }
        const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;

        const QString ref = parts.first().toUpper();
        if (refSeen.contains(ref)) {
            summary.warnings.append(QString("Duplicate element reference %1 in directive block (lines %2 and %3).").arg(parts.first()).arg(refSeen.value(ref)).arg(lineNo));
        } else {
            refSeen.insert(ref, lineNo);
        }
        summary.declaredElementRefs.insert(ref);

        const QChar prefix = ref.isEmpty() ? QChar() : ref.at(0);
        if ((prefix == 'V' || prefix == 'I') && parts.size() >= 2) {
            summary.drivenRailNets.insert(parts.at(1).trimmed().toUpper());
        }
    }

    if (!subcktStack.isEmpty()) {
        for (const QString& openSubckt : subcktStack) {
            summary.warnings.append(QString("Missing .ends for subcircuit %1.").arg(openSubckt));
        }
    }

    return summary;
}
}

QString SpiceNetlistGenerator::generate(QGraphicsScene* scene, const QString& projectDir, NetManager* /*netManager*/, const SimulationParams& params) {
    if (!scene) return "* Missing scene\n";

    qDebug() << "[SpiceNetlistGenerator] type=" << params.type << "rfPort1Source=" << params.rfPort1Source << "rfPort2Node=" << params.rfPort2Node << "rfZ0=" << params.rfZ0;

    QString netlist;
    netlist += "* viospice Automated Hierarchical SPICE Netlist\n";
    netlist += "* Generated: " + QDateTime::currentDateTime().toString(Qt::ISODate) + "\n";
    netlist += ".options ngbehavior=ltps\n";

    // 0. Append SPICE Directives from schematic at the TOP 
    // This ensures .params and .model are defined before use
    netlist += "* Custom SPICE Directives\n";
    
    // --- Tuning Slider Parameters ---
    for (QGraphicsItem* item : scene->items()) {
        if (auto* slider = dynamic_cast<TuningSliderSymbolItem*>(item)) {
            QString ref = slider->reference().trimmed();
            if (!ref.isEmpty()) {
                netlist += QString(".param %1 = %2\n").arg(ref).arg(slider->currentValue());
            }
        }
    }
    QSet<QString> switchModelsAdded;
    QSet<QString> userDeclaredModelFiles;
    QSet<QString> userElementRefs;
    QSet<QString> userDrivenRailNets;
    QStringList directiveWarnings;
    bool hasExplicitAnalysisCard = false;
    bool hasUserElementCards = false;
    bool hasExplicitSaveDirective = false;
    bool hasNetDirective = false;
    for (QGraphicsItem* item : scene->items()) {
        if (auto* si = dynamic_cast<SchematicItem*>(item)) {
            if (si->itemType() == SchematicItem::SpiceDirectiveType) {
                if (auto* dir = dynamic_cast<SchematicSpiceDirectiveItem*>(si)) {
                    QString cmd = dir->text().trimmed();
                    if (!cmd.isEmpty()) {
                        const UserSpiceContentSummary summary = summarizeUserSpiceText(cmd, projectDir);
                        userDeclaredModelFiles.unite(summary.declaredModelFiles);
                        switchModelsAdded.unite(summary.declaredModelNames);
                        userElementRefs.unite(summary.declaredElementRefs);
                        userDrivenRailNets.unite(summary.drivenRailNets);
                        directiveWarnings.append(summary.warnings);
                        hasExplicitAnalysisCard = hasExplicitAnalysisCard || summary.hasExplicitAnalysisCard;
                        hasUserElementCards = hasUserElementCards || summary.hasElementCards;
                        hasExplicitSaveDirective = hasExplicitSaveDirective || summary.hasExplicitSaveDirective;
                        hasNetDirective = hasNetDirective || summary.hasNetDirective;

                        const QStringList cmdLines = collapseSpiceContinuationLines(cmd);
                        int subcktDepth = 0;
                        for (const QString& rawCmdLine : cmdLines) {
                            const QString trimmedCmdLine = rawCmdLine.trimmed();
                            if (trimmedCmdLine.isEmpty()) {
                                netlist += "\n";
                                continue;
                            }

                            // Skip user .net directives for SParameter analysis - we generate our own in the correct location
                            if (params.type == SParameter && trimmedCmdLine.startsWith(".net", Qt::CaseInsensitive)) {
                                netlist += "* Skipped user .net directive (auto-generated for SParameter analysis): " + trimmedCmdLine + "\n";
                                continue;
                            }

                            // Skip user .ac directives for SParameter analysis - we generate our own in the correct location
                            if (params.type == SParameter && trimmedCmdLine.startsWith(".ac", Qt::CaseInsensitive)) {
                                netlist += "* Skipped user .ac directive (auto-generated for SParameter analysis): " + trimmedCmdLine + "\n";
                                continue;
                            }

                            const bool emulateStartupOnLine = summary.hasLtspiceStartup && subcktDepth == 0;
                            QString lineToWrite = rewriteLtspiceDirectiveLine(trimmedCmdLine, &directiveWarnings, emulateStartupOnLine, projectDir);

                            // Resolve relative .include/.lib/.inc paths to absolute so ngspice can always
                            // find them regardless of its CWD.
                            {
                                static const QRegularExpression incLineRe(
                                    "^(\\s*\\.(?:include|lib|inc)\\s+)(?:\"([^\"]+)\"|(\\S+))\\s*$",
                                    QRegularExpression::CaseInsensitiveOption);
                                const QRegularExpressionMatch m = incLineRe.match(lineToWrite);
                                if (m.hasMatch()) {
                                    const QString keyword = m.captured(1);
                                    const QString rawPath = m.captured(2).isEmpty() ? m.captured(3) : m.captured(2);
                                    QFileInfo rfInfo(rawPath);
                                    if (!rfInfo.isAbsolute()) {
                                        const QString absPath = normalizeIncludePathForNetlist(rawPath, projectDir);
                                        if (!absPath.isEmpty() && absPath != rawPath && QFileInfo::exists(absPath)) {
                                            lineToWrite = QString("%1\"%2\"").arg(keyword.trimmed() + " ", absPath);
                                            directiveWarnings.append(QString("Resolved relative include '%1' to '%2'.").arg(rawPath, absPath));
                                        }
                                    }
                                }
                            }

        if (trimmedCmdLine.startsWith(".mean", Qt::CaseInsensitive)) {
            const QString converted = normalizeMeanDirective(trimmedCmdLine);
            if (converted != trimmedCmdLine) {
                netlist += "* " + trimmedCmdLine + "\n";
                netlist += converted + "\n";
                updateSubcktDepthForLine(trimmedCmdLine, subcktDepth);
                continue;
            }
        }

        if (trimmedCmdLine.startsWith(".step", Qt::CaseInsensitive)) {
            netlist += "* " + trimmedCmdLine + "\n";
            netlist += "* LTspice .step omitted: this ngspice configuration reports .step as unimplemented\n";
            updateSubcktDepthForLine(trimmedCmdLine, subcktDepth);
            continue;
        }

                            if (trimmedCmdLine.startsWith(".meas", Qt::CaseInsensitive)) {
                                lineToWrite = normalizeLtspiceMeasDirective(lineToWrite, &directiveWarnings);
                            }

                            if (lineToWrite != trimmedCmdLine) {
                                netlist += "* LTspice rewrite: " + trimmedCmdLine + "\n";
                            }
                            netlist += lineToWrite + "\n";
                            updateSubcktDepthForLine(trimmedCmdLine, subcktDepth);
                        }
                    }
                }
            }
        }
    }
    netlist += "\n";

    // 0.5 Collect model includes from symbols
    QSet<QString> includePaths;
    QSet<QString> libPaths;

    // 1. Get Flattened ECO Package (Components)
    ECOPackage pkg = NetlistGenerator::generateECOPackage(scene, projectDir, nullptr);
    
    // 2. Get Flattened Connectivity (Nets)
    QList<NetlistNet> nets = NetlistGenerator::buildConnectivity(scene, projectDir, nullptr);

    // Build mapping: ComponentRef -> map(PinName -> NetName)
    // Gather pins from ALL units of the same component reference across the entire scene/hierarchy.
    QMap<QString, QMap<QString, QString>> componentPins;
    for (const auto& net : nets) {
        QString netName = net.name;
        if (netName.toUpper() == "GND" || netName == "0") netName = "0";
        for (const auto& pin : net.pins) {
            componentPins[pin.componentRef][pin.pinName] = netName;
        }
    }

    // Collect include paths from symbol metadata (subcircuit .inc/.lib)
    for (const auto& comp : pkg.components) {
        SymbolDefinition* sym = SymbolLibraryManager::instance().findSymbol(comp.typeName);
        if (!sym) continue;
        if (!sym->modelPath().isEmpty()) {
            const QString resolved = resolveModelPath(sym->modelPath(), projectDir);
            if (!resolved.isEmpty()) {
                if (resolved.toLower().endsWith(".lib")) libPaths.insert(resolved);
                else includePaths.insert(resolved);
            }
        }
    }

    // Auto-embed .model lines for referenced component models
    QStringList embeddedModelLines;
    QStringList runtimeWarnings;
    for (const auto& comp : pkg.components) {
        if (comp.excludeFromSim) continue;
        QString modelName = comp.value.trimmed();
        const QString typeLower = comp.typeName.trimmed().toLower();
        const bool isJfet = (typeLower == "njf" || typeLower == "pjf") ||
                            comp.reference.startsWith("J", Qt::CaseInsensitive);
        const bool isMos = (typeLower == "transistor_nmos" || typeLower == "transistor_pmos" ||
                            typeLower == "nmos" || typeLower == "nmos4" ||
                            typeLower == "pmos" || typeLower == "pmos4") ||
                           comp.reference.startsWith("M", Qt::CaseInsensitive);
        const bool isBjt = (typeLower == "transistor" || typeLower == "transistor_pnp" ||
                            typeLower == "npn" || typeLower == "npn2" || typeLower == "npn3" || typeLower == "npn4" ||
                            typeLower == "pnp" || typeLower == "pnp2" || typeLower == "pnp4" || typeLower == "lpnp") ||
                           comp.reference.startsWith("Q", Qt::CaseInsensitive);
        if (isMos || isJfet || isBjt) {
            modelName = splitLeadingSpiceToken(modelName).head;
        }
        if (modelName.isEmpty() && isJfet) {
            modelName = (typeLower == "pjf" || comp.reference.startsWith("JP", Qt::CaseInsensitive)) ? "PJF" : "NJF";
        }
        if (modelName.isEmpty() && isBjt) {
            modelName = (typeLower == "transistor_pnp" || typeLower == "pnp" || typeLower == "pnp2" ||
                         typeLower == "pnp4" || typeLower == "lpnp" ||
                         comp.reference.startsWith("QP", Qt::CaseInsensitive)) ? "2N3906" : "2N2222";
        }
        if (modelName.isEmpty() && isMos) {
            modelName = (typeLower == "transistor_pmos" || typeLower == "pmos" || typeLower == "pmos4" ||
                         comp.reference.startsWith("MP", Qt::CaseInsensitive)) ? "BS250" : "2N7000";
        }
        if (modelName.isEmpty()) continue;
        if (switchModelsAdded.contains(modelName.toLower())) {
            runtimeWarnings.append(QString("Skipped auto-generated model %1 because it is already declared manually.").arg(modelName));
            continue;
        }

        const SimModel* mdl = ModelLibraryManager::instance().findModel(modelName);
        if (mdl) {
            const QString line = modelToSpiceLine(*mdl);
            if (!line.isEmpty()) {
                embeddedModelLines.append(line);
                switchModelsAdded.insert(modelName.toLower());
            }
        } else if (ModelLibraryManager::instance().findSubcircuit(modelName) || 
                   comp.reference.startsWith("X", Qt::CaseInsensitive) || 
                   typeLower.contains("amplifier") || typeLower.contains("opamp") || typeLower.contains("ic")) {
            // If it's a subcircuit, we MUST ensure we have its pin names/order from the model library
            const SimSubcircuit* sub = ModelLibraryManager::instance().findSubcircuit(modelName);
            if (!sub) {
                // Force a quick indexing of the file if it's external, just in case
                QString subLib = ModelLibraryManager::instance().findLibraryPath(modelName);
                if (!subLib.isEmpty()) {
                    ModelLibraryManager::instance().loadLibraryFile(subLib);
                    sub = ModelLibraryManager::instance().findSubcircuit(modelName);
                }
            }

            QString subLib = ModelLibraryManager::instance().findLibraryPath(modelName);
            if (!subLib.isEmpty()) {
                if (subLib.endsWith(".lib", Qt::CaseInsensitive)) {
                    libPaths.insert(subLib);
                } else {
                    includePaths.insert(subLib);
                }
                // Ensure ModelLibraryManager has indexed this file so we can find subcircuit pin names for mapping
                ModelLibraryManager::instance().loadLibraryFile(subLib);
                switchModelsAdded.insert(modelName.toLower());
            }
        } else if (!switchModelsAdded.contains(modelName.toLower()) && comp.reference.startsWith("D", Qt::CaseInsensitive)) {
            // Generate .model from component paramExpressions for user-customized diodes
            const auto& pe = comp.paramExpressions;
            if (!pe.isEmpty()) {
                QString line = QString(".model %1 D(").arg(modelName);
                QStringList params;
                auto addParam = [&](const QString& key) {
                    QString val = pe.value(key).trimmed();
                    if (!val.isEmpty()) {
                        params.append(QString("%1=%2").arg(key, val));
                    }
                };
                addParam("diode.Is");
                addParam("diode.N");
                addParam("diode.Rs");
                addParam("diode.Vj");
                addParam("diode.Cjo");
                addParam("diode.M");
                addParam("diode.tt");
                addParam("diode.BV");
                addParam("diode.IBV");

                // Strip "diode." prefix for SPICE format
                for (int i = 0; i < params.size(); ++i) {
                    params[i].replace("diode.", "");
                }

                line += params.join(" ") + ")";
                embeddedModelLines.append(line);
                switchModelsAdded.insert(modelName.toLower());
            }
        } else if (isJfet) {
            // Generate .model from component paramExpressions for user-customized JFETs
            const auto& pe = comp.paramExpressions;
            if (!pe.isEmpty()) {
                const QString modelType = (typeLower == "pjf" || comp.reference.startsWith("JP", Qt::CaseInsensitive)) ? "PJF" : "NJF";
                QString line = QString(".model %1 %2(").arg(modelName, modelType);
                QStringList params;
                auto addParam = [&](const QString& key) {
                    QString val = pe.value(key).trimmed();
                    if (!val.isEmpty()) {
                        params.append(QString("%1=%2").arg(key, val));
                    }
                };
                addParam("jfet.Beta");
                addParam("jfet.Vto");
                addParam("jfet.Lambda");
                addParam("jfet.Rd");
                addParam("jfet.Rs");
                addParam("jfet.Cgs");
                addParam("jfet.Cgd");
                addParam("jfet.Is");

                for (int i = 0; i < params.size(); ++i) {
                    params[i].replace("jfet.", "");
                }

                line += params.join(" ") + ")";
                embeddedModelLines.append(line);
                switchModelsAdded.insert(modelName.toLower());
            }
        } else if (isBjt) {
            // Generate .model from component paramExpressions for user-customized BJTs
            const auto& pe = comp.paramExpressions;
            if (!pe.isEmpty()) {
                const QString bjtTypeExpr = pe.value("bjt.type").trimmed();
                const bool pnpFromExpr = bjtTypeExpr.compare("PNP", Qt::CaseInsensitive) == 0;
                const QString modelType = (pnpFromExpr ||
                                           typeLower == "transistor_pnp" || typeLower == "pnp" || typeLower == "pnp2" ||
                                           typeLower == "pnp4" || typeLower == "lpnp" ||
                                           comp.reference.startsWith("QP", Qt::CaseInsensitive)) ? "PNP" : "NPN";
                QString line = QString(".model %1 %2(").arg(modelName, modelType);
                QStringList params;
                auto addParam = [&](const QString& key) {
                    const QString val = pe.value(key).trimmed();
                    if (!val.isEmpty()) {
                        params.append(QString("%1=%2").arg(key, val));
                    }
                };
                addParam("bjt.Is");
                addParam("bjt.Bf");
                addParam("bjt.Vaf");
                addParam("bjt.Cje");
                addParam("bjt.Cjc");
                addParam("bjt.Tf");
                addParam("bjt.Tr");

                for (int i = 0; i < params.size(); ++i) {
                    params[i].replace("bjt.", "");
                }

                line += params.join(" ") + ")";
                embeddedModelLines.append(line);
                switchModelsAdded.insert(modelName.toLower());
            }
        } else if (isMos) {
            // Generate .model from component paramExpressions for user-customized MOSFETs
            const auto& pe = comp.paramExpressions;
            if (!pe.isEmpty()) {
                const QString mosTypeExpr = pe.value("mos.type").trimmed();
                const bool pmosFromExpr = mosTypeExpr.compare("PMOS", Qt::CaseInsensitive) == 0;
                const QString modelType = (pmosFromExpr ||
                                           typeLower == "transistor_pmos" || typeLower == "pmos" || typeLower == "pmos4" ||
                                           comp.reference.startsWith("MP", Qt::CaseInsensitive)) ? "PMOS" : "NMOS";
                QString line = QString(".model %1 %2(").arg(modelName, modelType);
                QStringList params;
                auto addParam = [&](const QString& key) {
                    const QString val = pe.value(key).trimmed();
                    if (!val.isEmpty()) {
                        params.append(QString("%1=%2").arg(key, val));
                    }
                };
                addParam("mos.Vto");
                addParam("mos.Kp");
                addParam("mos.Lambda");
                addParam("mos.Rd");
                addParam("mos.Rs");
                addParam("mos.Cgso");
                addParam("mos.Cgdo");

                for (int i = 0; i < params.size(); ++i) {
                    params[i].replace("mos.", "");
                }

                line += params.join(" ") + ")";
                embeddedModelLines.append(line);
                switchModelsAdded.insert(modelName.toLower());
            }
        }
    }

    // Write .include and .lib directives (subcircuit/model files from symbol metadata)
    if (!includePaths.isEmpty() || !libPaths.isEmpty()) {
        QStringList includeList = includePaths.values();
        includeList.sort();
        QStringList libList = libPaths.values();
        libList.sort();

        netlist += "* Model Includes\n";
        QSet<QString> emittedModelFiles = userDeclaredModelFiles;
        auto processPath = [&](const QString& inc, const QString& directive) {
            QString resolvedPath = normalizeIncludePathForNetlist(inc, projectDir);
            if (resolvedPath.isEmpty()) return;

            if (emittedModelFiles.contains(resolvedPath)) return;

            QString emittedPath = resolvedPath;
            QString emittedDirective = directive;
            if (QFileInfo::exists(resolvedPath)) {
                emittedPath = sanitizeModelIncludeForNgspice(resolvedPath);
                emittedPath = QDir::fromNativeSeparators(QDir::cleanPath(emittedPath));
                // ngspice accepts plain model/subckt files via .include. Using
                // .lib for standalone cached files causes parse failures because
                // there is no section selector.
                emittedDirective = "include";
                if (emittedModelFiles.contains(emittedPath)) return;
            }

            netlist += QString(".%1 \"%2\"\n").arg(emittedDirective, emittedPath);
            emittedModelFiles.insert(resolvedPath);
            emittedModelFiles.insert(emittedPath);
        };

        for (const QString& inc : includeList) processPath(inc, "include");
        for (const QString& lib : libList) processPath(lib, "lib");
        netlist += "\n";
    }

    // Write embedded .model lines
    if (!embeddedModelLines.isEmpty()) {
        netlist += "* Embedded Models\n";
        for (const QString& ml : embeddedModelLines) {
            netlist += ml + "\n";
        }
        netlist += "\n";
    }

    // 3. Global Power Net Mapping for hidden pin auto-connection
    QMap<QString, QString> powerNetMapping; // "VCC" -> "Net5", "VEE" -> "Net6"
    QSet<QString> emittedPowerSymbols; // Track power symbols to avoid processing duplicates
    for (const auto& comp : pkg.components) {
        if (comp.type == SchematicItem::PowerType) {
            QString ref = comp.reference;
            QMap<QString, QString> pins = componentPins.value(ref);
            QString netName = pickPowerNetName(pins, comp.value);
            
            // For power symbols, we only skip if it's the SAME reference AND SAME net.
            QString emitKey = ref + ":" + netName;
            if (emittedPowerSymbols.contains(emitKey)) continue;
            emittedPowerSymbols.insert(emitKey);
            
            if (netName.isEmpty()) continue;
            
            QString v = inferPowerVoltage(netName, comp.value);
            double val = 0.0;
            SimValueParser::parseSpiceNumber(v, val);
            
            const QString uNet = netName.trimmed().toUpper();
            const QString uVal = comp.value.trimmed().toUpper();

            if (val > 0) {
                if (!powerNetMapping.contains("VCC") || uNet.contains("VCC"))
                    powerNetMapping["VCC"] = netName;
            } else if (val < 0) {
                if (!powerNetMapping.contains("VEE") || uNet.contains("VEE"))
                    powerNetMapping["VEE"] = netName;
            }
            
            // Explicit name matching
            if (uNet == "VCC" || uNet == "VDD" || uNet == "V+" || uVal == "VCC" || uVal == "V+") 
                powerNetMapping["VCC"] = netName;
            else if (uNet == "VEE" || uNet == "VSS" || uNet == "V-" || uVal == "VEE" || uVal == "V-") 
                powerNetMapping["VEE"] = netName;
            else if (uNet == "GND" || uNet == "0" || uVal == "GND" || uVal == "0") 
                powerNetMapping["GND"] = "0";
        }
    }

    // 4. Export components
    QMap<QString, QString> powerNetVoltages;
    QStringList savedCurrentVectors;
    QSet<QString> emittedRefs;
    QSet<QString> digitalDrivenNets;
    NetlistManager::BridgeModels mixedModeBridgeModels;
    auto maybeDigitalNet = [&](const QString& netName) {
        const QString net = netName.trimmed().replace(' ', '_');
        if (!net.isEmpty() && net != "0") digitalDrivenNets.insert(net);
    };
    for (const auto& comp : pkg.components) {
        if (comp.excludeFromSim) continue;
        const QString ref = comp.reference;
        const QString rawLogicToken = comp.spiceModel.trimmed().isEmpty() ? comp.value.trimmed() : comp.spiceModel.trimmed();
        const bool isADevice = isXspiceLogicComponent(rawLogicToken, comp.typeName, ref);
        const QString codeModel = normalizeXspiceModelAlias(rawLogicToken, comp.typeName);
        if (!isADevice || usesNativeLogicADevice(codeModel)) continue;

        SymbolDefinition* sym = SymbolLibraryManager::instance().findSymbol(comp.typeName);
        const QMap<QString, QString> pins = componentPins.value(ref);
        for (auto it = pins.constBegin(); it != pins.constEnd(); ++it) {
            const QString heuristicPinName = pinNameForHeuristics(sym, it.key());
            bool hasDomainMetadata = false;
            const NodeType domain = pinDomainFromMetadata(sym, it.key(), &hasDomainMetadata);
            bool hasDirectionMetadata = false;
            const NetlistManager::PinDirection direction = pinDirectionFromMetadata(sym, it.key(), &hasDirectionMetadata);

            if (hasDomainMetadata && domain == NodeType::DIGITAL_EVENT &&
                hasDirectionMetadata &&
                (direction == NetlistManager::PinDirection::OUTPUT || direction == NetlistManager::PinDirection::BIDIRECTIONAL)) {
                maybeDigitalNet(it.value());
                continue;
            }

            if (isLikelyLogicOutputPinName(heuristicPinName)) {
                maybeDigitalNet(it.value());
            }
        }
    }

    if (!digitalDrivenNets.isEmpty()) {
        netlist += "* Mixed-mode XSPICE bridges\n";
        netlist += QString(".model __viospice_adc_bridge adc_bridge(in_low=%1 in_high=%2 rise_delay=%3 fall_delay=%4)\n")
                       .arg(QString::number(mixedModeBridgeModels.adcLow, 'g', 12),
                            QString::number(mixedModeBridgeModels.adcHigh, 'g', 12),
                            QString::number(mixedModeBridgeModels.adcRiseDelay, 'g', 12),
                            QString::number(mixedModeBridgeModels.adcFallDelay, 'g', 12));
        netlist += QString(".model __viospice_dac_bridge dac_bridge(out_low=%1 out_high=%2 out_undef=%3 input_load=%4 t_rise=%5 t_fall=%6)\n")
                       .arg(QString::number(mixedModeBridgeModels.dacLow, 'g', 12),
                            QString::number(mixedModeBridgeModels.dacHigh, 'g', 12),
                            QString::number(mixedModeBridgeModels.dacUndef, 'g', 12),
                            QString::number(mixedModeBridgeModels.dacInputLoad, 'g', 12),
                            QString::number(mixedModeBridgeModels.dacRiseTime, 'g', 12),
                            QString::number(mixedModeBridgeModels.dacFallTime, 'g', 12));
        netlist += ".subckt __viospice_adc_wrap ANA DIG\n";
        netlist += "* XSPICE adc_bridge: analog wire into event-driven digital logic.\n";
        netlist += "A_ADC [ANA] [DIG] __viospice_adc_bridge\n";
        netlist += ".ends __viospice_adc_wrap\n";
        netlist += ".subckt __viospice_dac_wrap DIG ANA\n";
        netlist += "* XSPICE dac_bridge: event-driven digital logic into analog wire.\n";
        netlist += "A_DAC [DIG] [ANA] __viospice_dac_bridge\n";
        netlist += ".ends __viospice_dac_wrap\n\n";
    }

    for (const auto& comp : pkg.components) {
        if (comp.excludeFromSim) {
            netlist += "* Skipping " + comp.reference + " (Excluded from simulation)\n";
            continue;
        }

        QString ref = comp.reference;
        const QString refKey = ref.trimmed().toUpper();
        
        const int type = comp.type;
        QString value = comp.value;
        const QString typeName = comp.typeName;

        // Power symbols often share the same '#' reference but represent different nets.
        // We handle them separately before the general duplicate check.
        if (type == SchematicItem::PowerType) {
            const QMap<QString, QString> pins = componentPins.value(ref);
            QString netName = pickPowerNetName(pins, value);
            if (!netName.isEmpty() && netName.toUpper() != "GND" && netName != "0") {
                const QString v = inferPowerVoltage(netName, value);
                powerNetVoltages[netName] = v;
                if (userDrivenRailNets.contains(netName.toUpper())) {
                    runtimeWarnings.append(QString("Manual directive source already drives schematic power rail %1; skipped auto-generated rail source.").arg(netName));
                }
            }
            continue;
        }

        if (userElementRefs.contains(refKey)) {
            runtimeWarnings.append(QString("Manual directive element %1 collides with schematic reference %2.").arg(ref, ref));
        }

        if (emittedRefs.contains(refKey)) {
            netlist += "* Skipping duplicate packaged unit " + ref + "\n";
            continue;
        }
        emittedRefs.insert(refKey);

        QMap<QString, QString> pins = componentPins.value(ref);
        

        QString line;

        // Helper to ensure proper SPICE prefix without doubling it
        auto ensurePrefix = [](const QString& r, const QString& p) -> QString {
            if (r.startsWith(p, Qt::CaseInsensitive)) return r;
            return p + r;
        };

        // Determine SPICE prefix
        bool isInstrument = (comp.typeName == "OscilloscopeInstrument" ||
                             comp.typeName == "Oscilloscope Instrument" ||
                             comp.typeName == "VoltmeterInstrument" ||
                             comp.typeName == "Voltmeter (DC)" ||
                             comp.typeName == "Voltmeter (AC)" ||
                             comp.typeName == "AmmeterInstrument" ||
                             comp.typeName == "Ammeter (DC)" ||
                             comp.typeName == "Ammeter (AC)" ||
                             comp.typeName == "WattmeterInstrument" ||
                             comp.typeName == "Wattmeter" ||
                             comp.typeName == "FrequencyCounterInstrument" ||
                             comp.typeName == "Frequency Counter" ||
                             comp.typeName == "LogicProbeInstrument" ||
                             comp.typeName == "Logic Probe");

        if (isInstrument) {
            QStringList keys = pins.keys();
            std::sort(keys.begin(), keys.end());
            for (const QString& pk : keys) {
                QString node = pins[pk].replace(" ", "_");
                if (node.isEmpty() || node.toUpper().startsWith("NC")) continue;
                if (node == "0") continue; // No need to ground ground

                netlist += QString("R_%1_%2 %3 0 100Meg\n").arg(ref, pk, node);
            }
            continue;
        }

        // Determine SPICE prefix
        if (type == SchematicItem::ResistorType) line = ensurePrefix(ref, "R");
        else if (type == SchematicItem::CapacitorType) line = ensurePrefix(ref, "C");
        else if (type == SchematicItem::InductorType) line = ensurePrefix(ref, "L");
        else if (type == SchematicItem::DiodeType) line = ensurePrefix(ref, "D");
        else if (type == SchematicItem::TransistorType) line = ensurePrefix(ref, "Q");
        else if (type == SchematicItem::VoltageSourceType) {
            if (comp.value.trimmed().startsWith("V=", Qt::CaseInsensitive)) line = ensurePrefix(ref, "B");
            else line = ensurePrefix(ref, "V");
        }
        else if (type == SchematicItem::SmartSignalType) {
            line = ensurePrefix(ref, "V"); // Controlled by FluxScript JIT
            value = "0"; // Initial value
        }
        else if (isXspiceLogicComponent(comp.spiceModel.trimmed().isEmpty() ? comp.value.trimmed() : comp.spiceModel.trimmed(),
                                        comp.typeName,
                                        ref)) {
            line = ensurePrefix(ref, "A"); // XSPICE A-device
        }
        else line = ensurePrefix(ref, "X"); // Subcircuit or generic
        // Fallback: if we don't know the type but reference has a known prefix,
        // use the reference as-is to avoid invalid X-lines.
        if (line.startsWith("X") && !ref.isEmpty()) {
            const QChar p = ref.at(0).toUpper();
            const QString known = "RCLVIDQMBEGFHJZ";
            if (known.contains(p)) {
                line = ref;
            }
        }
        const bool isADevice = line.startsWith("A", Qt::CaseInsensitive);
        const QString normalizedLogicCodeModel =
            isADevice ? normalizeXspiceModelAlias(comp.spiceModel.trimmed().isEmpty() ? comp.value.trimmed() : comp.spiceModel.trimmed(),
                                                  comp.typeName)
                      : QString();
        const bool isNativeLogicADevice = isADevice && usesNativeLogicADevice(normalizedLogicCodeModel);

        // --- SPICE Mapper Logic ---
        value = comp.value;
        if (!comp.spiceModel.isEmpty()) value = comp.spiceModel;

        // Standardize WAVEFILE and CHAN to space-separated syntax for better parser compatibility
        if (value.contains("WAVEFILE", Qt::CaseInsensitive)) {
            // Resolve relative path if needed
            QRegularExpression reFile(R"(WAVEFILE\s*=\s*\"([^\"]+)\")", QRegularExpression::CaseInsensitiveOption);
            auto match = reFile.match(value);
            if (match.hasMatch()) {
                QString rawPath = match.captured(1);
                QString targetPath = rawPath;
                QFileInfo fi(rawPath);
                if (!fi.isAbsolute() && !projectDir.isEmpty()) {
                    targetPath = QDir(projectDir).absoluteFilePath(rawPath);
                }
                value = QString("WAVEFILE \"%1\"").arg(targetPath);
            }

            QRegularExpression reChan(R"(CHAN\s*=\s*(\d+))", QRegularExpression::CaseInsensitiveOption);
            auto matchChan = reChan.match(comp.value); // Check original comp.value if model didn't have it
            if (!matchChan.hasMatch()) matchChan = reChan.match(value);
            if (matchChan.hasMatch()) {
                value += " CHAN " + matchChan.captured(1);
            }
        } else if (value.contains("FILE=", Qt::CaseInsensitive)) {
            // Resolve relative paths for other FILE= references
            QRegularExpression reFile(R"(FILE\s*=\s*\"([^\"]+)\")", QRegularExpression::CaseInsensitiveOption);
            auto match = reFile.match(value);
            if (match.hasMatch()) {
                QString rawPath = match.captured(1);
                QFileInfo fi(rawPath);
                if (!fi.isAbsolute() && !projectDir.isEmpty()) {
                    QString absPath = QDir(projectDir).absoluteFilePath(rawPath);
                    value.replace(rawPath, absPath);
                }
            }
        }

        value = inlinePwlFileIfNeeded(value, projectDir, nullptr);
        value = formatPwlValueForNetlist(value);
        QString instanceSuffix;
        QStringList nodes;
        const SimSubcircuit* activeSub = nullptr;

        // Find Symbol definition to check for custom mapping
        SymbolDefinition* sym = SymbolLibraryManager::instance().findSymbol(comp.typeName);
        if (sym) {
            // --- AUTO-CONNECT MISSING PINS (Hidden or unplaced Units) ---
            // Use existing 'pins' from outer scope

            // Check symbol definition for pins not present on the schematic
            const auto& symPins = sym->primitives();
            for (const auto& prim : symPins) {
                if (prim.type == Flux::Model::SymbolPrimitive::Pin) {
                    const QString pNum = QString::number(prim.data.value("number").toInt());
                    if (!pins.contains(pNum)) {
                        // Candidate for auto-connection
                        QString pName = prim.data.value("name").toString().toUpper();
                        if (pName == "VCC" || pName == "V+" || pName == "VDD") pins[pNum] = powerNetMapping.value("VCC", "VCC");
                        else if (pName == "VEE" || pName == "V-" || pName == "VSS") pins[pNum] = powerNetMapping.value("VEE", "VEE");
                        else if (pName == "GND" || pName == "0") pins[pNum] = powerNetMapping.value("GND", "0");
                        else pins[pNum] = "0"; // Default fallback
                    }
                }
            }
            
            // Ensure we use the updated mapping
            if (!sym->spiceModelName().isEmpty() && comp.spiceModel.isEmpty()) value = sym->spiceModelName();

            if (!sym->modelName().isEmpty() && comp.spiceModel.isEmpty()) {
                const QString mn = sym->modelName();
                const bool isX = line.startsWith("X");
                const bool isD = line.startsWith("D");
                const bool isQ = line.startsWith("Q");
                const bool isM = line.startsWith("M");

                if (isX || isD || isQ || isM || isADevice) {
                    // For subcircuits and complex devices, sym->modelName() is usually the SPICE model/subckt name.
                    // Only skip if it's just the single-letter prefix (legacy placeholder).
                    if (mn.length() > 1 || mn.toLower() != line.left(1).toLower()) {
                        value = mn;
                    }
                }
            }

            if (!sym->modelPath().isEmpty()) {
                const QString resolved = resolveModelPath(sym->modelPath(), projectDir);
                if (resolved.isEmpty() || !QFileInfo::exists(resolved)) {
                    netlist += QString("* Warning: Model file '%1' not found for %2\n").arg(sym->modelPath(), ref);
                }
            }

            if (!sym->modelName().isEmpty()) {
                // Skip warning if modelName is just the device prefix letter
                const QString mn = sym->modelName();
                bool isPrefixOnly = (mn.length() == 1 && mn.toLower() == line.left(1).toLower());
                if (!isPrefixOnly) {
                    const SimModel* mdl = ModelLibraryManager::instance().findModel(mn);
                    const SimSubcircuit* sub = ModelLibraryManager::instance().findSubcircuit(mn);
                    if (!mdl && !sub) {
                        netlist += QString("* Warning: Model '%1' not found for %2\n").arg(mn, ref);
                    } else if (sub) {
                        int symPinsCount = sym->connectionPoints().size();
                        auto mappingPins = sym->spiceNodeMapping();
                        if (!comp.pinPadMapping.isEmpty()) {
                            mappingPins.clear();
                            for (auto it = comp.pinPadMapping.constBegin(); it != comp.pinPadMapping.constEnd(); ++it) {
                                bool ok = false;
                                const int symbolPin = it.key().toInt(&ok);
                                if (ok && !it.value().trimmed().isEmpty()) {
                                    mappingPins.insert(symbolPin, it.value().trimmed());
                                }
                            }
                        }
                        if (!mappingPins.isEmpty() && line.startsWith("X", Qt::CaseInsensitive)) {
                            // For subcircuits with explicit mapping, compare against mapped simulation pins
                            // instead of raw drawable symbol pins (which may contain extra NC/alt-unit pins).
                            symPinsCount = mappingPins.size();
                        }
                        const int subPins = static_cast<int>(sub->pinNames.size());
                        if (symPinsCount > 0 && subPins > 0 && symPinsCount != subPins) {
                            netlist += QString("* Warning: Pin count mismatch for %1 (symbol %2 vs subckt %3)\n")
                                               .arg(ref)
                                               .arg(symPinsCount)
                                               .arg(subPins);
                        }
                    }
                    if (!activeSub && sub) activeSub = sub;
                }
            }
            
            QMap<int, QString> mapping = sym->spiceNodeMapping();
            if (!comp.pinPadMapping.isEmpty()) {
                mapping.clear();
                for (auto it = comp.pinPadMapping.constBegin(); it != comp.pinPadMapping.constEnd(); ++it) {
                    bool ok = false;
                    const int symbolPin = it.key().toInt(&ok);
                    if (ok && !it.value().trimmed().isEmpty()) {
                        mapping.insert(symbolPin, it.value().trimmed());
                    }
                }
            }
            if (!mapping.isEmpty()) {
                // KiCad Sim.Pins mapping is typically: symbolPinNumber -> subcktPinName.
                // If we know the active subckt signature, emit nodes in its formal pin order.
                if (line.startsWith("X", Qt::CaseInsensitive)) {
                    if (!activeSub && !value.trimmed().isEmpty()) {
                        activeSub = ModelLibraryManager::instance().findSubcircuit(value.trimmed());
                    }

                    if (activeSub) {
                        QMap<QString, int> subPinToSymbolPin;
                        for (auto it = mapping.constBegin(); it != mapping.constEnd(); ++it) {
                            subPinToSymbolPin.insert(it.value().trimmed().toUpper(), it.key());
                        }

                        for (const std::string& sp : activeSub->pinNames) {
                            const QString subPin = QString::fromStdString(sp).trimmed();
                            QString net = "0";

                            const int symbolPinNo = subPinToSymbolPin.value(subPin.toUpper(), -1);
                            if (symbolPinNo >= 0) {
                                net = pins.value(QString::number(symbolPinNo), QString());
                            }
                            // Fallbacks for symbol sets that key by pin names/tokens.
                            if (net.isEmpty()) net = fuzzyMatchPin(pins, subPin);
                            if (net.isEmpty()) net = pins.value(subPin, QString());
                            if (net.isEmpty()) net = "0";

                            nodes.append(net.replace(" ", "_"));
                        }
                    } else {
                        QList<int> sortedIndices = mapping.keys();
                        std::sort(sortedIndices.begin(), sortedIndices.end());
                        for (int idx : sortedIndices) {
                            QString pinName = mapping[idx];
                            QString net = pins.value(pinName, "0").replace(" ", "_");
                            if (net == "0") {
                                net = pins.value(QString::number(idx), "0").replace(" ", "_");
                            }
                            nodes.append(net);
                        }
                    }
                } else {
                    QList<int> sortedIndices = mapping.keys();
                    std::sort(sortedIndices.begin(), sortedIndices.end());
                    for (int idx : sortedIndices) {
                        QString pinName = mapping[idx];
                        QString net = pins.value(pinName, "0").replace(" ", "_");
                        if (net == "0") {
                            net = pins.value(QString::number(idx), "0").replace(" ", "_");
                        }
                        nodes.append(net);
                    }
                }
            }
        }
        
        if (nodes.isEmpty() && type == SchematicItem::TransistorType) {
            // Hardcoded mapping for TransistorItem if no symbol definition provides it
            // TransistorItem pins: 0=B/G, 1=C/D, 2=E/S
            // ngspice expects: BJT=C B E, MOSFET=D G S
            QString b_g = pins.value("B", pins.value("G", "0")).replace(" ", "_");
            QString c_d = pins.value("C", pins.value("D", "0")).replace(" ", "_");
            QString e_s = pins.value("E", pins.value("S", "0")).replace(" ", "_");
            
            nodes.append(c_d);
            nodes.append(b_g);
            nodes.append(e_s);
        }

        auto isEffectivelyDisconnectedNode = [&](const QString& nodeName) {
            const QString node = nodeName.trimmed();
            return node.isEmpty() || node == "0" || node.startsWith("NC_", Qt::CaseInsensitive);
        };

        if (nodes.isEmpty()) {
            // Default: Fallback to natural sorting of pins
            QStringList sortedKeys = pins.keys();
            std::sort(sortedKeys.begin(), sortedKeys.end(), naturalPinLessThan);
            
            if (sortedKeys.isEmpty()) {
                netlist += "* Skipping " + ref + " (no connections)\n";
                continue;
            }

            // XSPICE A-device vector grouping: [in1 in2 ...] out
            // For the built-in digital gates we target here, inputs may be vectorized
            // and bus-style pins like A0..A3 or D[0]..D[7] are collapsed into
            // a single XSPICE vector token. Scalar outputs stay scalar; grouped
            // bus outputs become [out0 out1 ...] only when the symbol actually
            // exposes multiple indexed outputs.
            if (isADevice) {
                QList<XspicePinAssignment> assignments;
                assignments.reserve(sortedKeys.size());
                int order = 0;
                for (const QString& pk : sortedKeys) {
                    QString net = pins[pk].replace(" ", "_");
                    if (net.isEmpty()) net = "NC_" + ref;
                    const QString heuristicPinName = pinNameForHeuristics(sym, pk);

                    bool hasDomainMetadata = false;
                    const NodeType domain = pinDomainFromMetadata(sym, pk, &hasDomainMetadata);
                    bool hasDirectionMetadata = false;
                    const NetlistManager::PinDirection direction = pinDirectionFromMetadata(sym, pk, &hasDirectionMetadata);
                    const bool isExplicitDigitalInput = hasDomainMetadata && domain == NodeType::DIGITAL_EVENT &&
                                                        hasDirectionMetadata && direction == NetlistManager::PinDirection::INPUT;
                    const bool shouldTreatAsInput = isExplicitDigitalInput ||
                                                    (!hasDirectionMetadata && isLikelyLogicInputPinName(heuristicPinName));

                    if (shouldTreatAsInput && !isNativeLogicADevice) {
                        if (!digitalDrivenNets.contains(net)) {
                            const QString bridgedNet = QString("__MM_ADC_%1_%2").arg(sanitizeMixedModeToken(ref), sanitizeMixedModeToken(pk));
                            netlist += mixedModeAdcBridgeLine(ref, pk, net, bridgedNet) + "\n";
                            runtimeWarnings.append(QString("Inserted adc_bridge on %1.%2 so analog net %3 can drive XSPICE digital input.").arg(ref, pk, net));
                            net = bridgedNet;
                        }
                    }

                    XspicePinAssignment assignment;
                    assignment.pinIdentifier = pk;
                    assignment.pinName = heuristicPinName;
                    assignment.netName = net;
                    assignment.isInput = shouldTreatAsInput;
                    assignment.order = order++;
                    assignment.vector = vectorPinInfo(sym, pk, heuristicPinName);
                    assignments.append(assignment);
                }

                bool hasInput = false;
                for (const XspicePinAssignment& assignment : assignments) {
                    if (assignment.isInput) {
                        hasInput = true;
                        break;
                    }
                }
                if (!hasInput && assignments.size() >= 2) {
                    for (int i = 0; i < assignments.size() - 1; ++i) {
                        assignments[i].isInput = true;
                    }
                    assignments.last().isInput = false;
                }

                if (isNativeLogicADevice) {
                    QStringList nativeNodes(8, QStringLiteral("0"));
                    QList<XspicePinAssignment> inputs;
                    QList<XspicePinAssignment> outputs;
                    for (const XspicePinAssignment& assignment : assignments) {
                        if (assignment.isInput) inputs.append(assignment);
                        else outputs.append(assignment);
                    }

                    auto upperName = [](const XspicePinAssignment& assignment) {
                        return assignment.pinName.trimmed().toUpper();
                    };
                    auto takeByNames = [upperName](QList<XspicePinAssignment>& pool, std::initializer_list<const char*> names) -> QString {
                        for (const char* rawName : names) {
                            const QString wanted = QString::fromLatin1(rawName);
                            for (int i = 0; i < pool.size(); ++i) {
                                if (upperName(pool[i]) == wanted) {
                                    const QString net = pool.takeAt(i).netName;
                                    return net.isEmpty() ? QStringLiteral("0") : net;
                                }
                            }
                        }
                        return QString();
                    };
                    auto takeFirst = [](QList<XspicePinAssignment>& pool) -> QString {
                        if (pool.isEmpty()) return QString();
                        const QString net = pool.takeFirst().netName;
                        return net.isEmpty() ? QStringLiteral("0") : net;
                    };
                    auto placeIf = [&](int index, const QString& net) {
                        if (index >= 0 && index < nativeNodes.size() && !net.isEmpty()) nativeNodes[index] = net;
                    };

                    if (normalizedLogicCodeModel == "d_dff") {
                        placeIf(0, takeByNames(inputs, {"D"}));
                        placeIf(1, takeByNames(inputs, {"CLK", "CLOCK", "CK", "C"}));
                    } else if (normalizedLogicCodeModel == "d_jkff") {
                        placeIf(0, takeByNames(inputs, {"J"}));
                        placeIf(1, takeByNames(inputs, {"K"}));
                        placeIf(2, takeByNames(inputs, {"CLK", "CLOCK", "CK", "C"}));
                    } else if (normalizedLogicCodeModel == "d_tff") {
                        placeIf(0, takeByNames(inputs, {"T"}));
                        placeIf(1, takeByNames(inputs, {"CLK", "CLOCK", "CK", "C"}));
                    } else if (normalizedLogicCodeModel == "d_srff" || normalizedLogicCodeModel == "d_srlatch") {
                        placeIf(0, takeByNames(inputs, {"S", "SET"}));
                        placeIf(1, takeByNames(inputs, {"R", "RESET"}));
                    } else if (normalizedLogicCodeModel == "d_dlatch") {
                        placeIf(0, takeByNames(inputs, {"D"}));
                        placeIf(1, takeByNames(inputs, {"CLK", "CLOCK", "CK", "C", "EN", "G", "GATE"}));
                    } else if (normalizedLogicCodeModel == "d_ram") {
                        int slot = 0;
                        while (slot < 5 && !inputs.isEmpty()) nativeNodes[slot++] = takeFirst(inputs);
                    } else {
                        int slot = 0;
                        while (slot < 5 && !inputs.isEmpty()) nativeNodes[slot++] = takeFirst(inputs);
                    }

                    QList<XspicePinAssignment> outputPool = assignments;
                    for (int i = outputPool.size() - 1; i >= 0; --i) {
                        if (outputPool[i].isInput) outputPool.removeAt(i);
                    }
                    QString q = takeByNames(outputPool, {"Q", "Y", "OUT"});
                    QString nq = takeByNames(outputPool, {"NQ", "QN", "QB", "OUTB", "YB"});
                    if (q.isEmpty()) q = takeFirst(outputPool);
                    if (nq.isEmpty()) nq = takeFirst(outputPool);
                    placeIf(6, q);
                    placeIf(7, nq);

                    nodes = nativeNodes;
                } else {
                    const QString pendingCodeModel = normalizeXspiceModelAlias(value, comp.typeName);
                    nodes = buildXspiceNodeTokens(assignments, xspiceModelUsesCollapsedInputVector(pendingCodeModel));
                }
            } else {
                for (const QString& pk : sortedKeys) {
                    QString net = pins[pk];
                    if (net.isEmpty()) net = "NC_" + ref;
                    nodes.append(net.replace(" ", "_"));
                }
            }
        }

        const bool isSemiconductorInstance =
            line.startsWith("D", Qt::CaseInsensitive) ||
            line.startsWith("M", Qt::CaseInsensitive) ||
            line.startsWith("Q", Qt::CaseInsensitive);
        if (isSemiconductorInstance && !nodes.isEmpty()) {
            bool allDisconnected = true;
            for (const QString& node : nodes) {
                if (!isEffectivelyDisconnectedNode(node)) {
                    allDisconnected = false;
                    break;
                }
            }
            if (allDisconnected) {
                netlist += "* Skipping " + ref + " (unconnected live duplicate)\n";
                continue;
            }
        }

        const QString currentSaveVector = currentSaveVectorForRef(line);
        if (!currentSaveVector.isEmpty() && !savedCurrentVectors.contains(currentSaveVector, Qt::CaseInsensitive)) {
            savedCurrentVectors.append(currentSaveVector);
        }

        // Strip unsupported voltage parasitics and emit separate elements for ngspice.
        const bool isVoltageSource = (type == SchematicItem::VoltageSourceType) ||
                                     comp.typeName.startsWith("Voltage_Source", Qt::CaseInsensitive);
        if (isVoltageSource) {
            VoltageParasitics paras = stripVoltageParasitics(value);
            value = paras.value;
            const bool hasRser = !paras.rser.isEmpty() && paras.rser != "0" && paras.rser != "0.0";
            const bool hasCpar = !paras.cpar.isEmpty() && paras.cpar != "0" && paras.cpar != "0.0";
            if ((hasRser || hasCpar) && nodes.size() >= 2) {
                QString n1 = nodes.value(0, "0");
                QString n2 = nodes.value(1, "0");
                QString srcPos = n1;
                if (hasRser) {
                    QString nInt = QString("VSR_%1").arg(ref);
                    nInt.replace(QRegularExpression("[^A-Za-z0-9_]"), "_");
                    netlist += QString("R_%1 %2 %3 %4\n").arg(ref, n1, nInt, paras.rser);
                    srcPos = nInt;
                }
                if (hasCpar) {
                    netlist += QString("C_%1 %2 %3 %4\n").arg(ref, srcPos, n2, paras.cpar);
                }
                    nodes[0] = srcPos;
                nodes[1] = n2;
            }
        }

        const bool isCurrentSource = (comp.type == SchematicItem::CurrentSourceType);
        if (isCurrentSource) {
            VoltageParasitics paras = stripVoltageParasitics(value);
            value = paras.value;
            QString rewrittenCurrentSource;
            if (rewriteLtspiceCurrentSourceSpecial(ref, nodes.value(0, "0"), nodes.value(1, "0"), value, projectDir,
                                                   &rewrittenCurrentSource, &directiveWarnings)) {
                netlist += rewrittenCurrentSource + "\n";
                continue;
            }
        }

        const bool isBehavioralCurrentSource = (comp.typeName.compare("Current_Source_Behavioral", Qt::CaseInsensitive) == 0) ||
                                              (comp.typeName.compare("bi", Qt::CaseInsensitive) == 0) ||
                                              (comp.typeName.compare("bi2", Qt::CaseInsensitive) == 0);
        if (isBehavioralCurrentSource) {
            QString n1 = nodes.value(0, "0");
            QString n2 = nodes.value(1, "0");

            const QString arrowDir = comp.paramExpressions.value("bi.arrow_direction").trimmed().toLower();
            const bool swapForUpArrow = (arrowDir == "up") || (comp.typeName.compare("bi2", Qt::CaseInsensitive) == 0);
            if (swapForUpArrow) {
                const QString tmp = n1;
                n1 = n2;
                n2 = tmp;
            }

            QString expr = value.trimmed();
            if (expr.isEmpty()) expr = "I=0";
            if (!expr.startsWith("I=", Qt::CaseInsensitive)) expr = "I=" + expr;

            QString bref = ref;
            if (!bref.startsWith("B", Qt::CaseInsensitive)) bref = "B" + ref;
            netlist += QString("%1 %2 %3 %4\n").arg(bref, n1, n2, expr);
            continue;
        }

        const bool isVCVS = (comp.typeName.compare("e", Qt::CaseInsensitive) == 0) ||
                            (comp.typeName.compare("vcvs", Qt::CaseInsensitive) == 0) ||
                            ref.startsWith("E", Qt::CaseInsensitive);
        const bool isVCCS = (comp.typeName.compare("g", Qt::CaseInsensitive) == 0) ||
                            (comp.typeName.compare("vccs", Qt::CaseInsensitive) == 0) ||
                            ref.startsWith("G", Qt::CaseInsensitive);

        if ((isVCVS || isVCCS)) {
            // Build nodes from pin numbers (pins map uses numeric keys "1","2","3","4")
            QStringList vcNodes;
            for (int i = 1; i <= 4; i++) {
                vcNodes.append(pins.value(QString::number(i), "0").replace(" ", "_"));
            }

            QString gain = value.trimmed();
            // Reject placeholder defaults like "E", "G", "g2", "vcvs", "vccs"
            const QString typeLower = comp.typeName.trimmed().toLower();
            const QString gainLower = gain.toLower();
            if (gain.isEmpty() || gainLower == typeLower ||
                gainLower == "e" || gainLower == "g" || gainLower == "g2" ||
                gainLower == "vcvs" || gainLower == "vccs") {
                gain = "1";
            }

            QString eref = ref;
            const QString pref = isVCVS ? "E" : "G";
            if (!eref.startsWith(pref, Qt::CaseInsensitive)) eref = pref + ref;
            netlist += QString("%1 %2 %3 %4 %5 %6\n").arg(eref, vcNodes[0], vcNodes[1], vcNodes[2], vcNodes[3], gain);
            continue;
        }

        const bool isCCCS = (comp.typeName.compare("f", Qt::CaseInsensitive) == 0) ||
                            (comp.typeName.compare("cccs", Qt::CaseInsensitive) == 0) ||
                            ref.startsWith("F", Qt::CaseInsensitive);
        const bool isCCVS = (comp.typeName.compare("h", Qt::CaseInsensitive) == 0) ||
                            (comp.typeName.compare("ccvs", Qt::CaseInsensitive) == 0) ||
                            ref.startsWith("H", Qt::CaseInsensitive);

        if ((isCCCS || isCCVS) && nodes.size() >= 2) {
            const QString n1 = nodes.at(0);
            const QString n2 = nodes.at(1);

            // Expecting value to be "VSOURCE GAIN" or similar
            QString controlSource;
            QString gainVal = "1";
            
            QStringList parts = value.split(" ", Qt::SkipEmptyParts);
            if (parts.size() >= 1) {
                controlSource = parts[0];
                if (parts.size() >= 2) gainVal = parts[1];
            } else {
                controlSource = "V_UNKNOWN_CTRL"; 
            }

            // Apply V-prefix rule for control source
            if (!controlSource.startsWith("V", Qt::CaseInsensitive)) {
                controlSource = "V" + controlSource;
            }

            QString eref = ref;
            const QString pref = isCCCS ? "F" : "H";
            if (!eref.startsWith(pref, Qt::CaseInsensitive)) eref = pref + ref;
            netlist += QString("%1 %2 %3 %4 %5\n").arg(eref, n1, n2, controlSource, gainVal);
            continue;
        }

        const bool isLosslessTLine = (comp.typeName.compare("tline", Qt::CaseInsensitive) == 0) ||
                                     ref.startsWith("T", Qt::CaseInsensitive);
        const bool isLossyTLine = (comp.typeName.compare("ltline", Qt::CaseInsensitive) == 0) ||
                                  ref.startsWith("O", Qt::CaseInsensitive);
        if ((isLosslessTLine || isLossyTLine) && nodes.size() >= 4) {
            const QString n1 = nodes.at(0);
            const QString n2 = nodes.at(1);
            const QString n3 = nodes.at(2);
            const QString n4 = nodes.at(3);

            if (isLossyTLine) {
                QString modelName = value.trimmed();
                if (modelName.isEmpty() || modelName.compare("LTRA", Qt::CaseInsensitive) == 0) {
                    modelName = "LTRAmod";
                }
                const QString r = comp.paramExpressions.value("ltra.R").trimmed();
                const QString l = comp.paramExpressions.value("ltra.L").trimmed();
                const QString g = comp.paramExpressions.value("ltra.G").trimmed();
                const QString c = comp.paramExpressions.value("ltra.C").trimmed();
                const QString len = comp.paramExpressions.value("ltra.LEN").trimmed();

                QStringList modelTokens;
                if (!r.isEmpty()) modelTokens << QString("R=%1").arg(r);
                if (!l.isEmpty()) modelTokens << QString("L=%1").arg(l);
                if (!g.isEmpty()) modelTokens << QString("G=%1").arg(g);
                if (!c.isEmpty()) modelTokens << QString("C=%1").arg(c);
                if (!len.isEmpty()) modelTokens << QString("LEN=%1").arg(len);

                if (!modelTokens.isEmpty() && !switchModelsAdded.contains(modelName.toLower())) {
                    netlist += QString(".model %1 LTRA(%2)\n").arg(modelName, modelTokens.join(" "));
                    switchModelsAdded.insert(modelName.toLower());
                }

                QString oref = ref;
                if (!oref.startsWith("O", Qt::CaseInsensitive)) oref = "O" + ref;
                netlist += QString("%1 %2 %3 %4 %5 %6\n").arg(oref, n1, n2, n3, n4, modelName);
            } else {
                QString z0 = "50";
                QString td = "50n";
                const QString v = value.trimmed();
                const QRegularExpression reZ0("\\bZ0\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
                const QRegularExpression reTd("\\bTd\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
                auto mz = reZ0.match(v);
                auto mt = reTd.match(v);
                if (mz.hasMatch()) z0 = mz.captured(1);
                if (mt.hasMatch()) td = mt.captured(1);

                QString tref = ref;
                if (!tref.startsWith("T", Qt::CaseInsensitive)) tref = "T" + ref;
                netlist += QString("%1 %2 %3 %4 %5 Z0=%6 Td=%7\n").arg(tref, n1, n2, n3, n4, z0, td);
            }
            continue;
        }

        const bool isNJF = (comp.typeName.compare("njf", Qt::CaseInsensitive) == 0);
        const bool isPJF = (comp.typeName.compare("pjf", Qt::CaseInsensitive) == 0);
        const bool isJFET = isNJF || isPJF || ref.startsWith("J", Qt::CaseInsensitive);
        if (isJFET && nodes.size() >= 3) {
            QString jref = ref;
            if (!jref.startsWith("J", Qt::CaseInsensitive)) jref = "J" + ref;

            QString model = value.trimmed();
            if (model.isEmpty() || model.compare("njf", Qt::CaseInsensitive) == 0 || model.compare("pjf", Qt::CaseInsensitive) == 0) {
                model = isPJF ? "2N5460" : "2N3819";
            }

            if (!switchModelsAdded.contains(model.toLower()) && !ModelLibraryManager::instance().findModel(model)) {
                const QString typeToken = isPJF ? "PJF" : "NJF";
                const QString vto = isPJF ? "2" : "-2";
                netlist += QString(".model %1 %2(Beta=1m Vto=%3 Lambda=0.02 Rd=1 Rs=1 Cgs=2p Cgd=1p Is=1e-14)\n")
                    .arg(model, typeToken, vto);
                switchModelsAdded.insert(model.toLower());
            }

            const QString d = nodes.at(0);
            const QString g = nodes.at(1);
            const QString s = nodes.at(2);
            netlist += QString("%1 %2 %3 %4 %5\n").arg(jref, d, g, s, model);
            continue;
        }

        const bool isMesfet = (comp.typeName.compare("mesfet", Qt::CaseInsensitive) == 0) ||
                               ref.startsWith("Z", Qt::CaseInsensitive);
        if (isMesfet && nodes.size() >= 3) {
            QString zref = ref;
            if (!zref.startsWith("Z", Qt::CaseInsensitive)) zref = "Z" + ref;

            QString model = value.trimmed();
            if (model.isEmpty()) model = "NMF";

            if (!switchModelsAdded.contains(model.toLower()) && !ModelLibraryManager::instance().findModel(model)) {
                const bool pchannel = model.compare("PMF", Qt::CaseInsensitive) == 0;
                const QString mtype = pchannel ? "PMF" : "NMF";
                const QString vto = pchannel ? "2.1" : "-2.1";
                netlist += QString(".model %1 %2(Vto=%3 Beta=0.05 Lambda=0.02 Alpha=3 B=0.5 Rd=1 Rs=1 Cgs=1p Cgd=0.2p)\n")
                    .arg(model, mtype, vto);
                switchModelsAdded.insert(model.toLower());
            }

            const QString d = nodes.at(0);
            const QString g = nodes.at(1);
            const QString s = nodes.at(2);
            netlist += QString("%1 %2 %3 %4 %5\n").arg(zref, d, g, s, model);
            continue;
        }

        const bool isVoltageControlledSwitch = (comp.typeName.compare("Voltage Controlled Switch", Qt::CaseInsensitive) == 0);
        if (isVoltageControlledSwitch) {
            const QString n1 = nodes.value(0, "0");
            const QString n2 = nodes.value(1, "0");
            const QString ctrlp = nodes.value(2, "0");
            const QString ctrln = nodes.value(3, "0");

            QString modelName = comp.paramExpressions.value("switch.model_name").trimmed();
            if (modelName.isEmpty()) modelName = QString("SW_%1").arg(ref);

            QString ron = comp.paramExpressions.value("switch.ron").trimmed();
            if (ron.isEmpty()) ron = "0.1";
            QString roff = comp.paramExpressions.value("switch.roff").trimmed();
            if (roff.isEmpty()) roff = "1Meg";
            QString vt = comp.paramExpressions.value("switch.vt").trimmed();
            if (vt.isEmpty()) vt = "0.5";
            QString vh = comp.paramExpressions.value("switch.vh").trimmed();
            if (vh.isEmpty()) vh = "0.1";

            if (!switchModelsAdded.contains(modelName)) {
                netlist += QString(".model %1 SW(Ron=%2 Roff=%3 Vt=%4 Vh=%5)\n")
                               .arg(modelName, ron, roff, vt, vh);
                switchModelsAdded.insert(modelName);
            }

            QString switchRef = ref;
            if (!switchRef.startsWith("S", Qt::CaseInsensitive)) switchRef = "S" + ref;
            netlist += QString("%1 %2 %3 %4 %5 %6\n").arg(switchRef, n1, n2, ctrlp, ctrln, modelName);
            continue;
        }

        const bool isCSW = (comp.typeName.compare("csw", Qt::CaseInsensitive) == 0) || ref.startsWith("W", Qt::CaseInsensitive);
        const bool isSwitch = (comp.typeName.compare("Switch", Qt::CaseInsensitive) == 0) ||
                              (comp.typeName.compare("sw", Qt::CaseInsensitive) == 0) ||
                              ref.startsWith("SW", Qt::CaseInsensitive) ||
                              ref.startsWith("S", Qt::CaseInsensitive) ||
                              isCSW;
        if (isSwitch) {
            // If the symbol provides control pins, treat it as a voltage-controlled switch.
            if (nodes.size() >= 4 && !isCSW) {
                const QString n1 = nodes.at(0);
                const QString n2 = nodes.at(1);
                const QString ctrlp = nodes.at(2);
                const QString ctrln = nodes.at(3);

                QString modelName = comp.paramExpressions.value("switch.model_name").trimmed();
                if (modelName.isEmpty()) modelName = QString("SW_%1").arg(ref);

                QString ron = comp.paramExpressions.value("switch.ron").trimmed();
                if (ron.isEmpty()) ron = "0.1";
                QString roff = comp.paramExpressions.value("switch.roff").trimmed();
                if (roff.isEmpty()) roff = "1Meg";
                QString vt = comp.paramExpressions.value("switch.vt").trimmed();
                if (vt.isEmpty()) vt = "0.5";
                QString vh = comp.paramExpressions.value("switch.vh").trimmed();
                if (vh.isEmpty()) vh = "0.1";

                if (!switchModelsAdded.contains(modelName)) {
                    netlist += QString(".model %1 SW(Ron=%2 Roff=%3 Vt=%4 Vh=%5)\n")
                                   .arg(modelName, ron, roff, vt, vh);
                    switchModelsAdded.insert(modelName);
                }

                QString switchRef = ref;
                if (!switchRef.startsWith("S", Qt::CaseInsensitive)) switchRef = "S" + ref;
                netlist += QString("%1 %2 %3 %4 %5 %6\n").arg(switchRef, n1, n2, ctrlp, ctrln, modelName);
                continue;
            }

            const QString n1 = nodes.value(0, "0");
            const QString n2 = nodes.value(1, "0");
            if (isCSW) {
                QString modelName = comp.paramExpressions.value("switch.model_name").trimmed();
                QString controlSource = comp.paramExpressions.value("switch.control_source").trimmed();

                if (modelName.isEmpty() && !value.isEmpty()) {
                    QStringList parts = value.split(" ", Qt::SkipEmptyParts);
                    if (parts.size() >= 2 && parts[0].startsWith("V", Qt::CaseInsensitive)) {
                        if (controlSource.isEmpty()) controlSource = parts[0];
                        modelName = parts[1];
                    } else if (parts.size() >= 1) {
                        modelName = parts[0];
                    }
                }

                if (modelName.isEmpty()) modelName = QString("CSW_%1").arg(ref);

                QString ron = comp.paramExpressions.value("switch.ron").trimmed();
                if (ron.isEmpty()) ron = comp.paramExpressions.value("csw.ron").trimmed();
                if (ron.isEmpty()) ron = "1";
                
                QString roff = comp.paramExpressions.value("switch.roff").trimmed();
                if (roff.isEmpty()) roff = comp.paramExpressions.value("csw.roff").trimmed();
                if (roff.isEmpty()) roff = "1Meg";
                
                QString it = comp.paramExpressions.value("switch.it").trimmed();
                if (it.isEmpty()) it = comp.paramExpressions.value("csw.it").trimmed();
                if (it.isEmpty()) it = "1m";
                
                QString ih = comp.paramExpressions.value("switch.ih").trimmed();
                if (ih.isEmpty()) ih = comp.paramExpressions.value("csw.ih").trimmed();
                if (ih.isEmpty()) ih = "0.2m";

                if (!switchModelsAdded.contains(modelName.toLower())) {
                    netlist += QString(".model %1 CSW(Ron=%2 Roff=%3 It=%4 Ih=%5)\n")
                                   .arg(modelName, ron, roff, it, ih);
                    switchModelsAdded.insert(modelName.toLower());
                }

                if (controlSource.isEmpty()) {
                    controlSource = "V_UNKNOWN_CTRL"; // Placeholder if user didn't specify
                } else if (!controlSource.startsWith("V", Qt::CaseInsensitive)) {
                    controlSource = "V" + controlSource;
                }
                QString switchRef = ref;
                if (!switchRef.startsWith("W", Qt::CaseInsensitive)) switchRef = "W" + ref;
                netlist += QString("%1 %2 %3 %4 %5\n").arg(switchRef, n1, n2, controlSource, modelName);
                continue;
            }

            const QString useModelExpr = comp.paramExpressions.value("switch.use_model").trimmed();
            const bool useModel = (useModelExpr == "1" || useModelExpr.compare("true", Qt::CaseInsensitive) == 0);

            if (useModel) {
                QString modelName = comp.paramExpressions.value("switch.model_name").trimmed();
                if (modelName.isEmpty()) modelName = QString("SW_%1").arg(ref);

                QString ron = comp.paramExpressions.value("switch.ron").trimmed();
                if (ron.isEmpty()) ron = "0.1";
                QString roff = comp.paramExpressions.value("switch.roff").trimmed();
                if (roff.isEmpty()) roff = "1Meg";
                QString vt = comp.paramExpressions.value("switch.vt").trimmed();
                if (vt.isEmpty()) vt = "0.5";
                QString vh = comp.paramExpressions.value("switch.vh").trimmed();
                if (vh.isEmpty()) vh = "0.1";

                if (!switchModelsAdded.contains(modelName)) {
                    netlist += QString(".model %1 SW(Ron=%2 Roff=%3 Vt=%4 Vh=%5)\n")
                                   .arg(modelName, ron, roff, vt, vh);
                    switchModelsAdded.insert(modelName);
                }

                QString switchRef = ref;
                if (!switchRef.startsWith("S", Qt::CaseInsensitive)) switchRef = "S" + ref;
                QString ctlNode = QString("SWCTL_%1").arg(ref);

                const QString stateExpr = comp.paramExpressions.value("switch.state").trimmed().toLower();
                const bool isOpen = (stateExpr.isEmpty() ? true : (stateExpr == "open"));

                bool okVt = false;
                bool okVh = false;
                const double vtNum = vt.toDouble(&okVt);
                const double vhNum = vh.toDouble(&okVh);
                const double vhAbs = okVh ? std::abs(vhNum) : 0.1;
                const double vtBase = okVt ? vtNum : 0.5;
                const double high = vtBase + vhAbs + 0.1;
                const double low = vtBase - vhAbs - 0.1;
                const double controlV = isOpen ? low : high;

                QString vref = QString("VSW_%1").arg(ref);
                if (!vref.startsWith("V", Qt::CaseInsensitive)) vref = "V" + vref;

                netlist += QString("%1 %2 %3 %4 0 %5\n").arg(switchRef, n1, n2, ctlNode, modelName);
                netlist += QString("%1 %2 0 DC %3\n").arg(vref, ctlNode, QString::number(controlV, 'g', 6));
                continue;
            }

            QString switchRef = ref;
            if (!switchRef.startsWith("R", Qt::CaseInsensitive)) switchRef = "R" + ref;
            QString switchValue = value.isEmpty() ? "1e12" : value;
            netlist += QString("%1 %2 %3 %4\n").arg(switchRef, n1, n2, switchValue);
            continue;
        }

        QStringList emittedNodes = nodes;
        if (!isADevice && !digitalDrivenNets.isEmpty()) {
            for (int nodeIdx = 0; nodeIdx < emittedNodes.size(); ++nodeIdx) {
                const QString node = emittedNodes.at(nodeIdx);
                if (node.startsWith("[") && node.endsWith("]")) continue;
                if (!digitalDrivenNets.contains(node)) continue;

                const QString bridgedNode = QString("__MM_DAC_%1_%2")
                                               .arg(sanitizeMixedModeToken(ref))
                                               .arg(nodeIdx + 1);
                const QString pinLabel = QString::number(nodeIdx + 1);
                netlist += mixedModeDacBridgeLine(ref, pinLabel, node, bridgedNode) + "\n";
                runtimeWarnings.append(QString("Inserted dac_bridge on %1 pin %2 so XSPICE digital net %3 can drive an analog/SPICE node.").arg(ref, pinLabel, node));
                emittedNodes[nodeIdx] = bridgedNode;
            }
        }
        if (line.startsWith("X", Qt::CaseInsensitive)) {
            const SimSubcircuit* subForCount = activeSub ? activeSub : nullptr;
            // Prefer the actual value token (model/subckt name) if already known.
            if (!subForCount && !value.trimmed().isEmpty()) {
                subForCount = ModelLibraryManager::instance().findSubcircuit(value.trimmed());
            }
            // Fallback to symbol model name.
            if (!subForCount && sym && !sym->modelName().trimmed().isEmpty()) {
                subForCount = ModelLibraryManager::instance().findSubcircuit(sym->modelName().trimmed());
            }

            if (subForCount) {
                const int subPins = static_cast<int>(subForCount->pinNames.size());
                if (subPins > 0) {
                    if (emittedNodes.size() > subPins) {
                        netlist += QString("* Warning: Trimming extra pins for %1 (%2 -> %3) to match subckt '%4'\n")
                                       .arg(ref)
                                       .arg(emittedNodes.size())
                                       .arg(subPins)
                                       .arg(QString::fromStdString(subForCount->name));
                        emittedNodes = emittedNodes.mid(0, subPins);
                    } else if (emittedNodes.size() < subPins) {
                        netlist += QString("* Warning: Padding missing pins for %1 (%2 -> %3) to match subckt '%4'\n")
                                       .arg(ref)
                                       .arg(emittedNodes.size())
                                       .arg(subPins)
                                       .arg(QString::fromStdString(subForCount->name));
                        while (emittedNodes.size() < subPins) emittedNodes.append("0");
                    }
                }
            }
        }

        if (line.startsWith("Q", Qt::CaseInsensitive) && emittedNodes.size() == 4) {
            const QString sub = emittedNodes.at(3).trimmed();
            if (sub.isEmpty() || sub == "0") {
                emittedNodes[3] = emittedNodes.at(2);
            }
        }

        for (const QString& node : emittedNodes) {
            line += " " + node;
        }

        // ngspice MOSFET requires 4 nodes: D G S B. For 3-pin symbols, tie body to source.
        if (line.startsWith("M", Qt::CaseInsensitive) && emittedNodes.size() == 3) {
            line += " " + emittedNodes.at(2);
        }

        auto isGenericBuiltInDiodePlaceholder = [&](const QString& candidate) -> bool {
            const QString normalized = candidate.trimmed().toLower();
            if (normalized.isEmpty()) return false;
            if (ModelLibraryManager::instance().findModel(candidate) ||
                ModelLibraryManager::instance().findSubcircuit(candidate)) {
                return false;
            }
            return normalized == "d" ||
                   normalized == "diode" ||
                   normalized == "diode_silicon" ||
                   normalized == "silicon";
        };

        if (line.startsWith("D") && isGenericBuiltInDiodePlaceholder(value)) {
            value.clear();
        }

        // Add value
        if (value.isEmpty()) {
            if (isADevice) {
                // LTspice digital symbols and generic logic symbols frequently store
                // aliases like AND, gate_and, DFF, BUF. XSPICE requires a .model
                // instance whose type is the real code model, e.g. d_and or d_dff.
                value = comp.paramExpressions.value("ltspice.SpiceModel").trimmed();
                if (value.isEmpty()) value = comp.paramExpressions.value("ltspice.MODEL").trimmed();
                if (value.isEmpty()) value = comp.paramExpressions.value("ltspice.Model").trimmed();
                if (value.isEmpty() && sym) {
                    if (!sym->spiceModelName().trimmed().isEmpty()) value = sym->spiceModelName().trimmed();
                    else if (!sym->modelName().trimmed().isEmpty()) value = sym->modelName().trimmed();
                }
                if (value.isEmpty()) {
                    const QString tl = comp.typeName.trimmed().toLower();
                    if (tl.contains("xnor")) value = "XNOR";
                    else if (tl.contains("xor")) value = "XOR";
                    else if (tl.contains("nand")) value = "NAND";
                    else if (tl.contains("nor")) value = "NOR";
                    else if (tl.contains("and")) value = "AND";
                    else if (tl.contains("or")) value = "OR";
                    else if (tl.contains("inv") || tl.contains("not")) value = "INV";
                    else if (tl.contains("buf")) value = "BUF";
                    else if (tl.contains("jk")) value = "JKFF";
                    else if (tl.contains("sr") && tl.contains("latch")) value = "SRLATCH";
                    else if (tl.contains("sr")) value = "SRFF";
                    else if (tl.contains("dlatch") || tl.contains("d_latch")) value = "DLATCH";
                    else if (tl.contains("dff") || tl.contains("flip")) value = "DFF";
                    else value = "AND";
                }
            } else
            if (line.startsWith("D")) {
                // Generate default .model for diodes with no model specified
                QString defaultModel = QString("D_DEFAULT_%1").arg(ref);
                netlist += QString(".model %1 D(Is=2.52n N=1.752 Rs=0.568 Vj=0.7 Cjo=4p M=0.4 tt=20n)\n")
                    .arg(defaultModel);
                value = defaultModel;
            } else if (line.startsWith("M", Qt::CaseInsensitive)) {
                const QString mosTypeExpr = comp.paramExpressions.value("mos.type").trimmed();
                const bool pmosAlias = mosTypeExpr.compare("PMOS", Qt::CaseInsensitive) == 0 ||
                                       comp.typeName.compare("Transistor_PMOS", Qt::CaseInsensitive) == 0 ||
                                       comp.typeName.compare("pmos", Qt::CaseInsensitive) == 0 ||
                                       comp.typeName.compare("pmos4", Qt::CaseInsensitive) == 0 ||
                                       ref.startsWith("MP", Qt::CaseInsensitive);
                value = pmosAlias ? "BS250" : "2N7000";
            } else if (line.startsWith("Q")) {
                const QString bjtTypeExpr = comp.paramExpressions.value("bjt.type").trimmed();
                const bool pnpAlias = bjtTypeExpr.compare("PNP", Qt::CaseInsensitive) == 0 ||
                                      comp.typeName.compare("transistor_pnp", Qt::CaseInsensitive) == 0 ||
                                      comp.typeName.compare("pnp", Qt::CaseInsensitive) == 0 ||
                                      comp.typeName.compare("pnp2", Qt::CaseInsensitive) == 0 ||
                                      comp.typeName.compare("pnp4", Qt::CaseInsensitive) == 0 ||
                                      comp.typeName.compare("lpnp", Qt::CaseInsensitive) == 0 ||
                                      ref.startsWith("QP", Qt::CaseInsensitive);
                value = pnpAlias ? "2N3906" : "2N2222";
            } else {
                value = "1k"; // Default for R/C/L
            }
        } else if (line.startsWith("M", Qt::CaseInsensitive) && (value.compare("NMOS", Qt::CaseInsensitive) == 0 || value.compare("PMOS", Qt::CaseInsensitive) == 0)) {
            value = (value.compare("PMOS", Qt::CaseInsensitive) == 0) ? "BS250" : "2N7000";
        } else if (line.startsWith("Q") && (value.compare("NPN", Qt::CaseInsensitive) == 0 || value.compare("PNP", Qt::CaseInsensitive) == 0)) {
            value = (value.compare("PNP", Qt::CaseInsensitive) == 0) ? "2N3906" : "2N2222";
        }

        if (line.startsWith("M", Qt::CaseInsensitive)) {
            const SpiceTokenSplit split = splitLeadingSpiceToken(value);
            if (!split.head.isEmpty()) {
                value = split.head;
                instanceSuffix = split.tail;
            }
        }

        if (line.startsWith("M", Qt::CaseInsensitive) && !switchModelsAdded.contains(value.toLower()) && !ModelLibraryManager::instance().findModel(value)) {
            const QString mosTypeExpr = comp.paramExpressions.value("mos.type").trimmed();
            const bool pmosModel = mosTypeExpr.compare("PMOS", Qt::CaseInsensitive) == 0 ||
                                   value.compare("BS250", Qt::CaseInsensitive) == 0 ||
                                   comp.typeName.compare("Transistor_PMOS", Qt::CaseInsensitive) == 0 ||
                                   comp.typeName.compare("pmos", Qt::CaseInsensitive) == 0 ||
                                   comp.typeName.compare("pmos4", Qt::CaseInsensitive) == 0 ||
                                   ref.startsWith("MP", Qt::CaseInsensitive);
            const QString mosType = pmosModel ? "PMOS" : "NMOS";
            const QString vto = pmosModel ? "-2" : "2";
            netlist += QString(".model %1 %2(Vto=%3 Kp=100u Lambda=0.02 Rd=1 Rs=1 Cgso=50p Cgdo=50p)\n")
                .arg(value, mosType, vto);
            switchModelsAdded.insert(value.toLower());
        }

        if (line.startsWith("Q") && !switchModelsAdded.contains(value.toLower()) && !ModelLibraryManager::instance().findModel(value)) {
            const QString bjtTypeExpr = comp.paramExpressions.value("bjt.type").trimmed();
            const bool pnpModel = bjtTypeExpr.compare("PNP", Qt::CaseInsensitive) == 0 ||
                                  value.compare("2N3906", Qt::CaseInsensitive) == 0 ||
                                  comp.typeName.compare("transistor_pnp", Qt::CaseInsensitive) == 0 ||
                                  comp.typeName.compare("pnp", Qt::CaseInsensitive) == 0 ||
                                  comp.typeName.compare("pnp2", Qt::CaseInsensitive) == 0 ||
                                  comp.typeName.compare("pnp4", Qt::CaseInsensitive) == 0 ||
                                  comp.typeName.compare("lpnp", Qt::CaseInsensitive) == 0 ||
                                  ref.startsWith("QP", Qt::CaseInsensitive);
            const QString bjtType = pnpModel ? "PNP" : "NPN";
            netlist += QString(".model %1 %2(Is=1e-14 Bf=100 Vaf=100 Cje=8p Cjc=3p Tf=400p Tr=50n)\n")
                .arg(value, bjtType);
            switchModelsAdded.insert(value.toLower());
        }

        if (isADevice && !isNativeLogicADevice) {
            const QString codeModel = normalizeXspiceModelAlias(value, comp.typeName);
            if (codeModel.isEmpty()) {
                runtimeWarnings.append(QString("Unknown XSPICE gate model '%1' on %2; defaulted to d_and.").arg(value, ref));
                value = QString("__XSPICE_%1").arg(sanitizeMixedModeToken(ref));
                const QString modelLine = defaultXspiceModelLine(ref, "d_and");
                if (!switchModelsAdded.contains(value.toLower())) {
                    netlist += modelLine + "\n";
                    switchModelsAdded.insert(value.toLower());
                }
            } else {
                const QString modelName = QString("__XSPICE_%1").arg(sanitizeMixedModeToken(ref));
                const QString modelLine = defaultXspiceModelLine(ref, codeModel);
                value = modelName;
                if (!switchModelsAdded.contains(modelName.toLower())) {
                    netlist += modelLine + "\n";
                    switchModelsAdded.insert(modelName.toLower());
                }
            }
        } else if (isNativeLogicADevice) {
            const QString nativeKeyword = nativeLogicKeywordForCodeModel(normalizedLogicCodeModel);
            if (!nativeKeyword.isEmpty()) {
                value = nativeKeyword;
            }
        }

        if ((line.startsWith("L", Qt::CaseInsensitive) || line.startsWith("C", Qt::CaseInsensitive)) &&
            emittedNodes.size() >= 2) {
            const PassiveCompanionParams passiveParams = parsePassiveCompanionParams(value);
            const bool hasCompanions = !passiveParams.rser.isEmpty() || !passiveParams.rpar.isEmpty() || !passiveParams.cpar.isEmpty();
            if (hasCompanions && !passiveParams.baseValue.isEmpty()) {
                const QString node1 = emittedNodes.at(0);
                const QString node2 = emittedNodes.at(1);
                const QString deviceNode1 = passiveParams.rser.isEmpty() ? node1 : QString("%1__rser").arg(ref);
                const QString elementRef = line.section(QRegularExpression("\\s+"), 0, 0).trimmed();

                if (!passiveParams.rser.isEmpty()) {
                    netlist += QString("R__RSER_%1 %2 %3 %4\n").arg(ref, node1, deviceNode1, passiveParams.rser);
                }
                if (!passiveParams.rpar.isEmpty()) {
                    netlist += QString("R__RPAR_%1 %2 %3 %4\n").arg(ref, deviceNode1, node2, passiveParams.rpar);
                }
                if (!passiveParams.cpar.isEmpty()) {
                    netlist += QString("C__CPAR_%1 %2 %3 %4\n").arg(ref, deviceNode1, node2, passiveParams.cpar);
                }

                QString mainLine = QString("%1 %2 %3 %4").arg(elementRef, deviceNode1, node2, passiveParams.baseValue);
                if (line.startsWith("L", Qt::CaseInsensitive) && !passiveParams.ic.isEmpty()) {
                    mainLine += " ic=" + passiveParams.ic;
                }
                mainLine += "\n";
                netlist += mainLine;

                runtimeWarnings.append(QString("Expanded inline parasitics on %1 into explicit companion elements for ngspice compatibility.").arg(ref));
                continue;
            }
        }

        line += " " + value;
        if (!instanceSuffix.isEmpty()) {
            line += " " + instanceSuffix;
        }
        if (!value.endsWith("\n")) line += "\n";
        
        netlist += line;
    }

    // 4. Generate Voltage Sources for Power Rails
    if (!directiveWarnings.isEmpty() || !runtimeWarnings.isEmpty()) {
        netlist += "* Directive Warnings\n";
        for (const QString& warning : directiveWarnings) {
            netlist += QString("* Warning: %1\n").arg(warning);
        }
        for (const QString& warning : runtimeWarnings) {
            netlist += QString("* Warning: %1\n").arg(warning);
        }
        netlist += "\n";
    }

    if (!hasUserElementCards && !powerNetVoltages.isEmpty()) {
        netlist += "\n* Power Supply Rails\n";
        for (auto it = powerNetVoltages.constBegin(); it != powerNetVoltages.constEnd(); ++it) {
            QString net = it.key();
            QString voltage = it.value();
            if (net.trimmed().isEmpty()) continue;

            if (userDrivenRailNets.contains(net.toUpper())) {
                continue;
            }
            
            QString spiceNet = QString(net).replace(" ", "_");
            netlist += QString("V_%1 %2 0 DC %3\n").arg(spiceNet).arg(spiceNet).arg(voltage);
        }
    }

    // 5. Simulation command
    netlist += "\n";

    // For SParameter (RF) analysis, always generate .ac and .net commands
    if (params.type == SParameter) {
        auto safeNumber = [](const QString& text, double fallback) {
            double parsed = 0.0;
            if (SimValueParser::parseSpiceNumber(text, parsed) && parsed > 0.0) {
                return text.trimmed();
            }
            return QString::number(fallback, 'g', 12);
        };

        const QString pts = safeNumber(params.step, 10.0);
        const QString start = safeNumber(params.start, 10.0);
        const QString stop = safeNumber(params.stop, 1e6);
        const QString z0 = params.rfZ0.isEmpty() ? "50" : params.rfZ0;

        netlist += QString(".ac dec %1 %2 %3\n").arg(pts, start, stop);
        netlist += QString(".net V(%1) %2 Rin=%3 Rout=%3\n")
                       .arg(params.rfPort2Node, params.rfPort1Source, z0);
    } else if (!hasExplicitAnalysisCard) {
        switch (params.type) {
            case Transient:
                netlist += QString(".tran %1 %2").arg(params.step, params.stop);
                if (!params.start.trimmed().isEmpty() && params.start.trimmed() != "0") {
                    netlist += QString(" %1").arg(params.start.trimmed());
                }
                if (params.transientSteady) {
                    netlist += " steady";
                }
                netlist += "\n";
                if (!params.steadyStateTol.trimmed().isEmpty()) {
                    netlist += QString(".options sstol=%1\n").arg(params.steadyStateTol.trimmed());
                }
                if (!params.steadyStateDelay.trimmed().isEmpty()) {
                    netlist += QString(".options ststdelay=%1\n").arg(params.steadyStateDelay.trimmed());
                }
                break;
            case DC:
                netlist += QString(".dc %1 %2 %3 %4\n").arg(params.dcSource, params.dcStart, params.dcStop, params.dcStep);
                break;
            case AC:
                {
                    auto safeNumber = [](const QString& text, double fallback) {
                        double parsed = 0.0;
                        if (SimValueParser::parseSpiceNumber(text, parsed) && parsed > 0.0) {
                            return text.trimmed();
                        }
                        return QString::number(fallback, 'g', 12);
                    };

                    const QString pts = safeNumber(params.step, 10.0);
                    const QString start = safeNumber(params.start, 10.0);
                    const QString stop = safeNumber(params.stop, 1e6);
                    netlist += QString(".ac dec %1 %2 %3\n").arg(pts, start, stop);
                }
                break;
            case OP:
                netlist += ".op\n";
                break;
            case Noise:
                {
                    const QString output = params.noiseOutput.isEmpty() ? "V(out)" : params.noiseOutput;
                    const QString source = params.noiseSource.isEmpty() ? "V1" : params.noiseSource;
                    const QString pts = params.step.isEmpty() ? "10" : params.step;
                    const QString fstart = params.start.isEmpty() ? "1" : params.start;
                    const QString fstop = params.stop.isEmpty() ? "1Meg" : params.stop;
                    netlist += QString(".noise %1 %2 %3 %4 %5\n").arg(output, source, pts, fstart, fstop);
                }
                break;
            case Fourier:
                {
                    const QString freq = params.fourFreq.isEmpty() ? "1k" : params.fourFreq;
                    QStringList outputs = params.fourOutputs;
                    if (outputs.isEmpty()) outputs << "V(out)";
                    netlist += QString(".four %1 %2\n").arg(freq, outputs.join(" "));
                }
                break;
            case TF:
                {
                    const QString output = params.tfOutput.isEmpty() ? "V(out)" : params.tfOutput;
                    const QString source = params.tfSource.isEmpty() ? "V1" : params.tfSource;
                    netlist += QString(".tf %1 %2\n").arg(output, source);
                }
                break;
            case Disto:
                {
                    const QString pts = params.step.isEmpty() ? "10" : params.step;
                    const QString fstart = params.start.isEmpty() ? "1" : params.start;
                    const QString fstop = params.stop.isEmpty() ? "1Meg" : params.stop;
                    if (!params.distoF2OverF1.isEmpty()) {
                        netlist += QString(".disto %1 %2 %3 %4\n").arg(pts, fstart, fstop, params.distoF2OverF1);
                    } else {
                        netlist += QString(".disto %1 %2 %3\n").arg(pts, fstart, fstop);
                    }
                }
                break;
            case Meas:
                if (!params.measRaw.isEmpty()) {
                    netlist += params.measRaw + "\n";
                }
                break;
            case Step:
                if (!params.stepRaw.isEmpty()) {
                    netlist += params.stepRaw + "\n";
                }
                break;
            case Sens:
                {
                    const QString output = params.sensOutput.isEmpty() ? "V(out)" : params.sensOutput;
                    netlist += QString(".sens %1\n").arg(output);
                }
                break;
            case FFT:
                // FFT is handled post-simulation, not a SPICE directive itself
                break;
        }
    }

    if (!hasExplicitSaveDirective) {
        netlist += ".save all\n";
        for (const QString& saveVec : savedCurrentVectors) {
            netlist += QString(".save %1\n").arg(saveVec);
        }
    }
    netlist += ".control\n";
    netlist += "set ngbehavior=ltps\n";
    // For SParameter analysis, save S-parameters to Touchstone file and load back
    if (params.type == SParameter) {
        netlist += "run\n";
        netlist += "wrs2p s_param.s2p\n";
        netlist += "setplot write\n";
    } else {
        netlist += "run\n";
    }
    netlist += ".endc\n.end\n";
    return netlist;
}

QString SpiceNetlistGenerator::buildCommand(const SimulationParams& params) {
    switch (params.type) {
        case Transient:
        {
            QString command = QString(".tran %1 %2").arg(params.step, params.stop);
            if (!params.start.trimmed().isEmpty() && params.start.trimmed() != "0") {
                command += QString(" %1").arg(params.start.trimmed());
            }
            if (params.transientSteady) {
                command += " steady";
            }
            return command;
        }
        case DC:
            return QString(".dc %1 %2 %3 %4").arg(params.dcSource, params.dcStart, params.dcStop, params.dcStep);
        case AC: {
            auto safeNumber = [](const QString& text, double fallback) {
                double parsed = 0.0;
                if (SimValueParser::parseSpiceNumber(text, parsed) && parsed > 0.0) {
                    return text.trimmed();
                }
                return QString::number(fallback, 'g', 12);
            };
            const QString pts = safeNumber(params.step, 10.0);
            const QString start = safeNumber(params.start, 10.0);
            const QString stop = safeNumber(params.stop, 1e6);
            return QString(".ac dec %1 %2 %3").arg(pts, start, stop);
        }
        case OP:
            return ".op";
        case Noise: {
            const QString output = params.noiseOutput.isEmpty() ? "V(out)" : params.noiseOutput;
            const QString source = params.noiseSource.isEmpty() ? "V1" : params.noiseSource;
            const QString pts = params.step.isEmpty() ? "10" : params.step;
            const QString fstart = params.start.isEmpty() ? "1" : params.start;
            const QString fstop = params.stop.isEmpty() ? "1Meg" : params.stop;
            return QString(".noise %1 %2 %3 %4 %5").arg(output, source, pts, fstart, fstop);
        }
        case Fourier: {
            const QString freq = params.fourFreq.isEmpty() ? "1k" : params.fourFreq;
            QStringList outputs = params.fourOutputs;
            if (outputs.isEmpty()) outputs << "V(out)";
            return QString(".four %1 %2").arg(freq, outputs.join(" "));
        }
        case TF: {
            const QString output = params.tfOutput.isEmpty() ? "V(out)" : params.tfOutput;
            const QString source = params.tfSource.isEmpty() ? "V1" : params.tfSource;
            return QString(".tf %1 %2").arg(output, source);
        }
        case Disto: {
            const QString pts = params.step.isEmpty() ? "10" : params.step;
            const QString fstart = params.start.isEmpty() ? "1" : params.start;
            const QString fstop = params.stop.isEmpty() ? "1Meg" : params.stop;
            if (!params.distoF2OverF1.isEmpty()) {
                return QString(".disto %1 %2 %3 %4").arg(pts, fstart, fstop, params.distoF2OverF1);
            }
            return QString(".disto %1 %2 %3").arg(pts, fstart, fstop);
        }
        case Meas:
            return params.measRaw.isEmpty() ? ".meas" : params.measRaw;
        case Step:
            return params.stepRaw.isEmpty() ? ".step" : params.stepRaw;
        case Sens: {
            const QString output = params.sensOutput.isEmpty() ? "V(out)" : params.sensOutput;
            return QString(".sens %1").arg(output);
        }
        case FFT:
            return ".fft";
        case SParameter: {
            const QString z0 = params.rfZ0.isEmpty() ? "50" : params.rfZ0;
            const QString p1 = params.rfPort1Source.isEmpty() ? "V1" : params.rfPort1Source;
            const QString p2 = params.rfPort2Node.isEmpty() ? "OUT" : params.rfPort2Node;
            return QString(".net V(%1) %2 Rin=%3 Rout=%4")
                .arg(p2, p1, z0, z0);
        }
    }
    return ".op";
}

QString SpiceNetlistGenerator::normalizeXspiceGateModelAlias(const QString& rawToken, const QString& typeName) {
    return ::normalizeXspiceModelAlias(rawToken, typeName);
}

QStringList SpiceNetlistGenerator::buildXspiceNodeTokensForPins(const QMap<QString, QString>& pins,
                                                                const Flux::Model::SymbolDefinition* symbol,
                                                                bool collapseScalarInputsToVector) {
    QStringList sortedKeys = pins.keys();
    std::sort(sortedKeys.begin(), sortedKeys.end(), naturalPinLessThan);

    QList<XspicePinAssignment> assignments;
    assignments.reserve(sortedKeys.size());

    int order = 0;
    for (const QString& key : sortedKeys) {
        XspicePinAssignment assignment;
        assignment.pinIdentifier = key;
        assignment.pinName = pinNameForHeuristics(symbol, key);
        assignment.netName = pins.value(key).replace(" ", "_");
        assignment.order = order++;

        bool hasDomainMetadata = false;
        const NodeType domain = pinDomainFromMetadata(symbol, key, &hasDomainMetadata);
        bool hasDirectionMetadata = false;
        const NetlistManager::PinDirection direction = pinDirectionFromMetadata(symbol, key, &hasDirectionMetadata);
        const bool isExplicitDigitalInput = hasDomainMetadata && domain == NodeType::DIGITAL_EVENT &&
                                            hasDirectionMetadata && direction == NetlistManager::PinDirection::INPUT;
        assignment.isInput = isExplicitDigitalInput ||
                             (!hasDirectionMetadata && isLikelyLogicInputPinName(assignment.pinName));
        assignment.vector = vectorPinInfo(symbol, key, assignment.pinName);
        assignments.append(assignment);
    }

    return buildXspiceNodeTokens(assignments, collapseScalarInputsToVector);
}

QString SpiceNetlistGenerator::formatValue(double value) {
    if (value <= 0) return "0";
    if (value < 1e-9) return QString::number(value * 1e12) + "p";
    if (value < 1e-6) return QString::number(value * 1e9) + "n";
    if (value < 1e-3) return QString::number(value * 1e06) + "u";
    if (value < 1) return QString::number(value * 1e3) + "m";
    return QString::number(value);
}

QString SpiceNetlistGenerator::generateCompatibilityLayer(const QString& rawNetlist) {
    if (rawNetlist.isEmpty()) return QString();

    QString processed = rawNetlist;
    processed = processed.trimmed();

    // Split by newline and handle potential different newline formats
    QStringList lines = processed.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
    QStringList outLines;
    QStringList warnings;

    for (QString line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('*') || trimmed.startsWith(';') || trimmed.startsWith('#') || trimmed.startsWith('+')) {
            outLines << line; // Keep continuation lines as-is, don't try to rewrite them
            continue;
        }

        // If the line already uses native VioMATRIXC pwlfile, don't mess with it
        if (trimmed.contains("pwlfile=", Qt::CaseInsensitive)) {
            outLines << line;
            continue;
        }

        line = rewriteLtspiceDirectiveLine(line, &warnings);

        outLines << line;
    }

    return outLines.join('\n');
}

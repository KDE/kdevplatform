/***************************************************************************
 *   Copyright 2008 Aleix Pol <aleixpol@gmail.com>                         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.         *
 ***************************************************************************/

#ifndef KROSSWRAPPER_H
#define KROSSWRAPPER_H

#include "duchainreader.h"

class KrossWrapper : public DUChainReader
{
    public:
        KrossWrapper(KDevelop::TopDUContext* top) : DUChainReader(top) {}
        QString output;
        QString handlersHeader;
        
        void writeDocument()
        {
            handlersHeader += "#ifndef "+filename.toUpper()+"_H\n";
            handlersHeader += "#define "+filename.toUpper()+"_H\n\n";
            handlersHeader += "#include<QtCore/QVariant>\n\n";
            handlersHeader += "//This is file has been generated by xmltokross, "
                              "you should not edit this file but the files used to generate it.\n\n";
                              
            output += "//This is file has been generated by xmltokross, you should not edit this file but the files used to generate it.\n\n"
                      "#include <QtCore/QObject>\n"
                      "#include <QtCore/QVariant>\n"
                      "#include <kross/core/manager.h>\n"
                      "#include <kross/core/wrapperinterface.h>\n";
                      "#include \""+filename+"\"\n";
            foreach(const QString & include, includes)
            {
                output += "#include <"+include+">\n";
//                 handlersHeader += "#include <"+include+">\n";
            }
            output +='\n';
        }
        
        QString toKrossName(const QString &name)
        {
            return "Kross"+QString(name).replace("::", QString());
        }
        
        void writeClass(const QString& classname, const QString& baseClass, const QList<QStringList>& enums)
        {
            classNamespace[classname]=inNamespace;
            
            qDebug() << "writeClass: " << classNamespace[classname] << "::"
                     << classname << definedClasses << " : public " << baseClass;
            QString krossClassname=toKrossName(classname);
            if(baseClass.isEmpty())
                output += "class " + krossClassname + " : public QObject, public Kross::WrapperInterface\n";
            else
                output += "class " + krossClassname + " : public "+toKrossName(baseClass)+'\n';
            
            output += "{\n"
                      "\tQ_OBJECT\n";
            
            foreach(const QStringList& en, enums)
            {
                writeQ_Enum(en);
            }
            
            output += "\tpublic:\n";
            
            foreach(const QStringList& en, enums)
            {
                writeEnum(en);
            }
            
            if(baseClass.isEmpty())
                output += "\t\t"+krossClassname+'('
                                 +classname+"* obj, QObject* parent=0) : QObject(parent), wrapped(obj)\t";
            else
                output += "\t\t"+krossClassname+"("+classname+"* obj, QObject* parent=0) : "+toKrossName(baseClass)+"(obj, parent), wrapped(obj)\n";
                
            output += "\t{ setObjectName(\""+classname+"\"); }\n\t\tvoid* wrappedObject() const { return wrapped; }\n\n";
        }
        
        void writeEndClass()
        { output += "\tprivate:\n"
                    "\t\t"+definedClasses.last()+"* wrapped;\n"
                    "};\n\n"; }
                    
        void writeVariable(const QString& name, const QString& _type, bool isConst)
        {
            bool isPtr=_type.contains('*');
            QString typeName=QString(_type).replace('*', QString());
            QString type=QString(typeName).replace("::", QString());
            if(type.contains(','))
            {
                qDebug() << "Can't put the member variable "<< name << "to the interface because the type has comma's (,)";
                return;
            }
            QString write;
            if(!isConst)
                write=" WRITE set"+name;
            if(_type.contains("::"))
                output += QString("\t\ttypedef %1 %2;\n").arg(typeName).arg(type);
            output += "\t\tQ_PROPERTY("+(isConst? "const " : QString())+type+(isPtr ? '*':' ')+' '+name+" READ get"+name+write+" SCRIPTABLE true)\n";
            if(!isConst) {
                QString setType="const "+type;
                
                output += "\t\tQ_SCRIPTABLE void set"+name+'('+setType+(isPtr ? '*':' ')+" val) { wrapped->"+name+"=val; }\n";
            }
            output += "\t\tQ_SCRIPTABLE "+(isConst? "const " : QString())+type+(isPtr ? '*':' ')+
                      " get"+name+"() const { return wrapped->"+name+"; }\n";
        }
        
        void writeNamespace(const QString& name)
        {
            output += "using namespace "+name+";\n\n";
        }
        
        void writeEndEnum(const QStringList& ) { Q_ASSERT(false); }
        
        void writeEnum(const QStringList &fl)
        {
            QStringList flags=fl;
            QString name=flags.first().right(flags.first().size()-flags.first().lastIndexOf(':')-1);
            flags.takeFirst();
            
            QStringList enumFlags;
            
            foreach(const QString& f, flags)
            {
                QString fname=f.right(f.size()-f.lastIndexOf(':')-1);
                enumFlags += fname+'='+f;
            }
            
            output += QString("\t\tenum Kross%1 { %2 };\n").arg(name).arg(enumFlags.join(", "));
        }
        
        void writeQ_Enum(const QStringList& fl)
        {
            QStringList flags=fl;
            QString name=flags.first().right(flags.first().size()-flags.first().lastIndexOf(':')-1);
            flags.takeFirst();
            output += QString("\tQ_ENUMS(%1)\n").arg(name);
            
            QStringList qFlags;
            
            foreach(const QString& f, flags)
            {
                QString fname=f.right(f.size()-f.lastIndexOf(':')-1);
                qFlags += fname;
            }
            
            output += QString("\tQ_FLAGS(%1 %2)\n\n").arg(name.right(name.size()-name.lastIndexOf(':')-1)).arg(qFlags.join(" "));
        }
        
        QString handlerName(const QString& classname)
        {
            QString handlername=QString(classname).replace("::", QString());
            handlername[0]=handlername[0].toLower();
            return handlername;
        }
        
        void createHandler(const QString& _classname)
        {
            //TODO: Should improve the memory management. Use harald's script tools.
            QString classname=QString(_classname).replace("::", QString());
            
            qDebug() << "creating handler " << classname << classNamespace;
            
            handlersHeader += "\tQVariant _"+handlerName(classname)+"Handler(void* type);\n";
            handlersHeader += "\tQVariant "+handlerName(classname)+"Handler("+_classname+"* type);\n";
            handlersHeader += "\tQVariant "+handlerName(classname)+"Handler(const "+_classname+"* type);\n\n";
            
            output += "QVariant _"+handlerName(classname)+"Handler(void* type)\n"
            "{\n"
            "\tif(!type) return QVariant();\n"
            "\t"+_classname+"* t=static_cast<"+_classname+"*>(type);\n"
            "\tQ_ASSERT(dynamic_cast<"+_classname+"*>(t));\n";
            bool first=true;
            QStringList sons=sonsPerClass[_classname];
            foreach(const QString& item, sons)
            {
                if(!first)
                    output+="\telse ";
                else
                    output+='\t';
                output += "if(dynamic_cast<"+item+"*>(t)) return _"+handlerName(item)+"Handler(type);\n";
                first=false;
            }
            if(sons.isEmpty())
                output+='\t';
            else
                output+="\telse ";
                
            output += "return qVariantFromValue((QObject*) new "+toKrossName(classname)+"(t, 0));\n"
                      "}\n";
            
            if(_classname.contains("::"))
            {
                int idx=_classname.indexOf("::")+2;
                output += "bool b_"+classname+"1="+filename+"_registerHandler(\""+
                    _classname.mid(idx, _classname.size()-idx)+"*\", _"+handlerName(classname)+"Handler);\n";
            }
            output += "bool b_"+classname+"="+filename+"_registerHandler(\""+_classname+"*\", _"+handlerName(classname)+"Handler);\n";
            output += "QVariant "+handlerName(classname)+"Handler("+_classname+"* type)"
                      "{ return _"+handlerName(classname)+"Handler(type); }\n";
            output += "QVariant "+handlerName(classname)+"Handler(const "+_classname+"* type) "
                      "{ return _"+handlerName(classname)+"Handler((void*) type); }\n\n";
        }
        
        void createFwd(const QString& classname)
        {
            QString classNS;
            if(classNamespace.contains(classname) && !classNamespace[classname].isEmpty())
                classNS=classNamespace[classname];
            
            handlersHeader += "namespace " +classNS+" { class "+classname.split("::").last()+"; }\n";
        }
        
        void writeEndDocument()
        {
            foreach(const QString& aclass, definedClasses)
                createFwd(aclass);
            
            output += "bool "+filename+"_registerHandler(const QByteArray& name, Kross::MetaTypeHandler::FunctionPtr* handler)\n"
                      "{ Kross::Manager::self().registerMetaTypeHandler(name, handler); return false; }\n\n";
            
            output += "namespace Handlers\n{\n";
            handlersHeader += "namespace Handlers\n{\n";
            
            QStringList::const_iterator it=definedClasses.constEnd();
            do {
                it--;
                createHandler(*it);
            }
            while(it!=definedClasses.begin());
            
            output += "}\n";
            handlersHeader += "}\n\n";
            output += "#include \""+filename+".moc\"\n";
            
            handlersHeader += "#endif\n";
        }
        
        void writeEndFunction(const method& m)
        {
            QString rettype=m.returnType;
            rettype=rettype.replace('&', QString());
            if(!rettype.contains('*'))
                rettype=rettype.replace("const ", QString());
            output += "\t\tQ_SCRIPTABLE " + rettype +' '+ m.funcname+'(';
            QStringList values;
            
            int param=0;
            foreach(const method::argument& arg, m.args)
            {
                QString varname=arg.name;
                if(varname.isEmpty()) {
                    qWarning() << "The paramenter number "+QString::number(param)+" in method: "+
                            inNamespace+"::"+definedClasses.last()+"::"+m.funcname+" does not have a name";
                    varname=QString("x%1").arg(param);
                }
                values += varname;
                output += arg.type +' '+ varname;
                if(!arg.def.isEmpty())
                    output+='='+arg.def;
                output += ", ";
                param++;
            }
            
            if(!values.isEmpty())
                output.resize(output.size()-2);
            output += ')';
            if(m.isConst)
                output+=" const";
            
            QString shouldReturn= m.returnType=="void" ? QString() : QString("return ");
            
            output += " { "+shouldReturn+"wrapped->"+m.funcname+"(";
            foreach(const QString& val, values)
            {
                output+=val+", ";
            }
            
            if(!values.isEmpty())
                output.resize(output.size()-2);
            
            output += "); }\n";
        }
};

#endif
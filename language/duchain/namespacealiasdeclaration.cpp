/* This  is part of KDevelop
    Copyright 2007 David Nolden <david.nolden.kdevelop@art-master.de>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "namespacealiasdeclaration.h"

#include "ducontext.h"
#include "declarationdata.h"
#include "duchainregister.h"

namespace KDevelop
{

class NamespaceAliasDeclarationData : public DeclarationData
{
public:
  NamespaceAliasDeclarationData() {}
  NamespaceAliasDeclarationData( const NamespaceAliasDeclarationData& rhs )
      : DeclarationData( rhs )
  {
  }
  IndexedQualifiedIdentifier m_importIdentifier; //The identifier that was imported
};

REGISTER_DUCHAIN_ITEM(NamespaceAliasDeclaration);

NamespaceAliasDeclaration::NamespaceAliasDeclaration(const NamespaceAliasDeclaration& rhs) 
  : Declaration(*new NamespaceAliasDeclarationData(*rhs.d_func())) {
  setSmartRange(rhs.smartRange(), DocumentRangeObject::DontOwn);
}

NamespaceAliasDeclaration::NamespaceAliasDeclaration(const SimpleRange& range, DUContext* context)
  : Declaration(*new NamespaceAliasDeclarationData, range)
{
  d_func_dynamic()->setClassId(this);
  setKind(NamespaceAlias);
  if( context )
    setContext( context );
}

NamespaceAliasDeclaration::NamespaceAliasDeclaration(NamespaceAliasDeclarationData& data) : Declaration(data) {
}


QualifiedIdentifier NamespaceAliasDeclaration::importIdentifier() const {
  return d_func()->m_importIdentifier.identifier();
}

void NamespaceAliasDeclaration::setImportIdentifier(const QualifiedIdentifier& id) {
  d_func_dynamic()->m_importIdentifier = id;
}

NamespaceAliasDeclaration::~NamespaceAliasDeclaration()
{
}

Declaration* NamespaceAliasDeclaration::clone() const {
  return new NamespaceAliasDeclaration(*this);
}

void NamespaceAliasDeclaration::setAbstractType(AbstractType::Ptr type) {
  //A namespace-import does not have a type, so ignore any set type
  Q_UNUSED(type);
}

QString NamespaceAliasDeclaration::toString() const {
  DUCHAIN_D(NamespaceAliasDeclaration);
  if( identifier() != globalImportIdentifier )
    return QString("Import %1 as %2").arg(d->m_importIdentifier.identifier().toString()).arg(identifier().toString());
  else
    return QString("Import %1").arg(d->m_importIdentifier.identifier().toString());
}

}
// kate: space-indent on; indent-width 2; tab-width 4; replace-tabs on; auto-insert-doxygen on

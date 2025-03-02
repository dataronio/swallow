#include "codegen.hpp"

#include "ast/ast.hpp"
#include "errors/error.hpp"
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace cpp {

Codegen::Codegen(std::string outputFilename, ast::AstNodePtr ast,std::string filename) {
    m_filename=filename;
    m_file.open(outputFilename);
    m_file << "#include <cstdio>\n#include <functional>\ntypedef enum{error___AssertionError,error___ZeroDivisionError} error;\n";
    m_env = createEnv();
    ast->accept(*this);
    m_file.close();
}

std::shared_ptr<SymbolTable<ast::AstNodePtr>>
Codegen::createEnv(std::shared_ptr<SymbolTable<ast::AstNodePtr>> parent) {
    return std::make_shared<SymbolTable<ast::AstNodePtr>>(parent);
}

// TODO: buffer it
std::string Codegen::write(std::string_view code) {
    if(save){
        res+=code;
    }
    else{
        m_file << code;
    }
    return res;
}

std::string Codegen::mangleName(ast::AstNodePtr astNode) {
    return std::string("");
}

std::string Codegen::searchDefaultModule(std::string path,
                                         std::string moduleName) {
    for (auto& entry : std::filesystem::directory_iterator(path)) {
        if (std::filesystem::path(entry.path()).filename() == moduleName) {
            // TODO :ignore extensions?
            if (entry.is_regular_file()) {
                return entry.path().string();
            } else if (entry.is_directory()) {
                // TODO: avoid deeply nested folders
                searchDefaultModule(entry.path().string(), moduleName);
            }
        }
    }

    return "";
}

void Codegen::codegenFuncParams(std::vector<ast::parameter> parameters,size_t start) {
    if (parameters.size()) {
        for (size_t i = start; i < parameters.size(); ++i) {
            if (i-start)
                write(", ");
            if (parameters[i].p_type->type()==ast::KAstNoLiteral){
                write("auto");
            }
            else{
                parameters[i].p_type->accept(*this);
            }
            write(" ");
            parameters[i].p_name->accept(*this);
            if (parameters[i].p_default->type()!=ast::KAstNoLiteral){
                write("=");
                parameters[i].p_default->accept(*this);
            }
        }
    }
}

bool Codegen::visit(const ast::Program& node) {
    for (auto& stmt : node.statements()) {
        stmt->accept(*this);
        write(";\n");
    }
    return true;
}

bool Codegen::visit(const ast::BlockStatement& node) {
    for (auto& stmt : node.statements()) {
        write("    ");
        stmt->accept(*this);
        write(";\n");
    }
    return true;
}

bool Codegen::visit(const ast::ImportStatement& node) { return true; }

bool Codegen::visit(const ast::FunctionDefinition& node) {
    auto functionName =
        std::dynamic_pointer_cast<ast::IdentifierExpression>(node.name())
            ->value();
    if (!is_func_def){
        is_func_def=true;
        if (functionName == "main") {
            // we want the main function to always return 0 if success
            write("int main (");
            codegenFuncParams(node.parameters());
            write(") {\n");
            node.body()->accept(*this);
            write("return 0;\n}");
        } else {
            if(node.returnType().size()==1){
                node.returnType()[0]->accept(*this);
            }
            else{
                write("void");
            }
            write(" ");
            node.name()->accept(*this);
            write("(");
            codegenFuncParams(node.parameters());
            if(node.returnType().size()>1){
                if(node.parameters().size()>0){
                    write(",");
                }
                for(size_t i=0;i<node.returnType().size();++i){
                    node.returnType()[i]->accept(*this);
                    write("* ____PEREGRINE____RETURN____"+std::to_string(i)+"=NULL");
                    if(i!=node.returnType().size()-1){
                        write(",");
                    }
                }
            }
            write(") {\n");
            node.body()->accept(*this);
            write("\n}");
        }
        is_func_def=false;
    }
    else{
        write("auto ");
        node.name()->accept(*this);
        write("=[=](");
        codegenFuncParams(node.parameters());
        if(node.returnType().size()==0){
            write(")mutable->void");
        }
        else if(node.returnType().size()==1){
            write(")mutable->");
            node.returnType()[0]->accept(*this);
        }
        else{
            if(node.parameters().size()>0){
                    write(",");
            }
            for(size_t i=0;i<node.returnType().size();++i){
                node.returnType()[i]->accept(*this);
                write("* ____PEREGRINE____RETURN____"+std::to_string(i)+"=NULL");
                if(i!=node.returnType().size()-1){
                    write(",");
                }
            }
            write(")mutable->void");
        }
        write(" {\n");
        node.body()->accept(*this);
        write("\n}");
    }
    return true;
}

bool Codegen::visit(const ast::VariableStatement& node) {
    if (node.varType()->type() != ast::KAstNoLiteral) {
        node.varType()->accept(*this);
        write(" ");
    }

    //TODO: We are getting this if input is a.b->u[9].h=9. Is is valid?
    //a.b->u.__getitem__(9).h = 9;

    ast::AstNodePtr name=node.name();
    while(name->type()==ast::KAstDotExpression){
      std::shared_ptr<ast::DotExpression> dot_exp=std::dynamic_pointer_cast<ast::DotExpression>(name);
      dot_exp->owner()->accept(*this);
      write(".");
      name=dot_exp->referenced();
    }
    while(name->type()==ast::KAstArrowExpression){
      std::shared_ptr<ast::ArrowExpression> arrow_exp=std::dynamic_pointer_cast<ast::ArrowExpression>(name);
      arrow_exp->owner()->accept(*this);
      write("->");
      name=arrow_exp->referenced();
    }

    if (name->type()==ast::KAstListOrDictAccess){
      std::shared_ptr<ast::ListOrDictAccess> access_exp=std::dynamic_pointer_cast<ast::ListOrDictAccess>(name);
      access_exp->container()->accept(*this);
      write(".__assignitem__(");
      access_exp->keyOrIndex()[0]->accept(*this);
      if(access_exp->keyOrIndex().size()==2){
          write(",");
          access_exp->keyOrIndex()[1]->accept(*this);
      }
      write(",");
      node.value()->accept(*this);
      write(")");
    }
    else{
      name->accept(*this);
      if (node.value()->type() != ast::KAstNoLiteral) {
          write(" = ");
          node.value()->accept(*this);
      }
    }
    return true;
}

bool Codegen::visit(const ast::ConstDeclaration& node) {
    write("const ");
    if (node.constType()->type()!=ast::KAstNoLiteral){
        node.constType()->accept(*this);
    }
    write(" ");
    node.name()->accept(*this);
    write("=");
    node.value()->accept(*this);
    return true;
    }

bool Codegen::visit(const ast::TypeDefinition& node) {
    write("typedef ");
    node.baseType()->accept(*this);
    write(" ");
    node.name()->accept(*this);
    return true;
}

bool Codegen::visit(const ast::PassStatement& node) {
    // we are making it a comment because ; is added
    // to each node at the end. we dont want that to
    // happen because it will result in ;; which is
    // an error
    write("\n//pass");
    return true;
}

bool Codegen::visit(const ast::IfStatement& node) {
    write("if (");
    node.condition()->accept(*this);
    write(") {\n");
    node.ifBody()->accept(*this);
    write("}");

    auto elifNode = node.elifs();
    if (elifNode.size() != 0) {
        write("\n");
        for (auto& body : elifNode) { // making sure that elif exists
            write("else if (");
            body.first->accept(*this);
            write(") {\n");
            body.second->accept(*this);
            write("}");
        }
    }

    auto elseNode = node.elseBody();
    if (elseNode->type() ==
        ast::KAstBlockStmt) { // making sure that else exists
        write("\nelse {\n");
        elseNode->accept(*this);
        write("}");
    }
    return true;
}

bool Codegen::visit(const ast::WhileStatement& node) {
    write("while (");
    node.condition()->accept(*this);
    write(") {\n");
    node.body()->accept(*this);
    write("}");
    return true;
}

bool Codegen::visit(const ast::ForStatement& node) {
    write("auto ____PEREGRINE____VALUE=");
    node.sequence()->accept(*this);
    write(";\n");
    write("for (size_t ____PEREGRINE____i=0;____PEREGRINE____i<____PEREGRINE____VALUE.__iter__();++____PEREGRINE____i){\n");
    if (node.variable().size()==1){
        write("auto ");
        node.variable()[0]->accept(*this);
        write("=____PEREGRINE____VALUE.__iterate__();\n");
    }
    else{
        write("auto ____PEREGRINE____TEMP=____PEREGRINE____VALUE.__iterate__();\n");
        for (size_t i=0;i<node.variable().size();++i){
            auto x=node.variable()[i];
            write("auto ");
            x->accept(*this);
            write("=____PEREGRINE____TEMP.__getitem__(");
            write(std::to_string(i));
            write(");\n");
        }
    }
    node.body()->accept(*this);
    write("\n}");
    return true;
}

bool Codegen::visit(const ast::MatchStatement& node) {
    auto toMatch = node.matchItem();
    auto cases = node.caseBody();
    auto defaultBody = node.defaultBody();
    write("\nwhile (true) {\n");
    for (size_t i = 0; i < cases.size(); ++i) {
        auto currCase = cases[i];
        if (currCase.first.size() == 1 &&
            currCase.first[0]->type() == ast::KAstNoLiteral) {
            if (i == 0) {
                currCase.second->accept(*this);
                write("\n");
            } else {
                write("else {\n");
                currCase.second->accept(*this);
                write("\n}\n");
            }
        } else if (i == 0) {
            write("if (");
            matchArg(toMatch, currCase.first);
            write(") {\n");
            currCase.second->accept(*this);
            write("\n}\n");
        } else {
            write("else if (");
            matchArg(toMatch, currCase.first);
            write(") {\n");
            currCase.second->accept(*this);
            write("\n}\n");
        }
    }

    if (defaultBody->type() != ast::KAstNoLiteral) {
        defaultBody->accept(*this);
    }
    write("\nbreak;\n}");
    return true;
}

bool Codegen::visit(const ast::ScopeStatement& node) {
    write("{\n");
    node.body()->accept(*this);
    write("\n}");
    return true;
}

bool Codegen::visit(const ast::CppStatement& node) {
    // we are making it a comment because ; is added to
    // each node at the end. we dont want that to happen
    // because it will result in ;; which is an error
    write(node.value() + "\n//");
    return true;
}

bool Codegen::visit(const ast::ReturnStatement& node) {
    if(node.returnValue().size() == 1){
        write("return ");
        node.returnValue()[0]->accept(*this);
    }
    else{
        write("if (____PEREGRINE____RETURN____0!=NULL){\n");
        for (size_t i=0;i<node.returnValue().size();++i){
            auto x=node.returnValue()[i];
            write("*____PEREGRINE____RETURN____"+std::to_string(i)+"=");
            x->accept(*this);
            write(";\n");
        }
        write("}\n");
    }
    return true;
}

bool Codegen::visit(const ast::ContinueStatement& node) {
    write("continue");
    return true;
}

bool Codegen::visit(const ast::BreakStatement& node) {
    write("break");
    return true;
}

bool Codegen::visit(const ast::DecoratorStatement& node) {
    auto items = node.decoratorItem();
    auto body = node.body();
    std::string contains;
    std::string x;
    std::string prev;
    save=true;
    if (res!=""){
        prev=res;
        res="";
    }
    if(body->type()==ast::KAstFunctionDef || body->type()==ast::KAstStatic){
        std::shared_ptr<ast::FunctionDefinition> function;
        if (body->type()==ast::KAstStatic){
            write("static ");
            function = std::dynamic_pointer_cast<ast::FunctionDefinition>(
                        std::dynamic_pointer_cast<ast::StaticStatement>(body)->body()
                        );
        }
        else{
            function = std::dynamic_pointer_cast<ast::FunctionDefinition>(body);
        }
        write("auto ");
        function->name()->accept(*this);
        write("=");
        x+=res;
        res="";
        if(is_func_def){
            write("[=](");
        }
        else{
            write("[](");
        }
        codegenFuncParams(function->parameters());
        if(function->returnType().size()==0){
            write(")mutable->void");
        }
        else if(function->returnType().size()==1){
            write(")mutable->");
            function->returnType()[0]->accept(*this);
        }
        else{
            if(function->parameters().size()>0){
                    write(",");
            }
            for(size_t i=0;i<function->returnType().size();++i){
                function->returnType()[i]->accept(*this);
                write("* ____PEREGRINE____RETURN____"+std::to_string(i)+"=NULL");
                if(i!=function->returnType().size()-1){
                    write(",");
                }
            }
            write(")mutable->void");
        }
        write("{\n");
        if(!is_func_def){
            is_func_def=true;
            function->body()->accept(*this);
            is_func_def=false;
        }
        else{
            function->body()->accept(*this);
        }
        write("\n}");
        contains=res;
        res="";
    }
    for (size_t i = items.size() - 1; i != (size_t)-1; i--){
        ast::AstNodePtr item=items[i];
        contains=wrap(item,contains);
    }
    if (prev==""){
        save=false;
        write(x+contains);
    }
    else{
        write(prev+x+contains);
    }
    return true;
}

bool Codegen::visit(const ast::ListLiteral& node) {
    write("{");
    auto elements=node.elements();
    if (elements.size()>0){
        for (size_t i=0;i<elements.size();++i){
            elements[i]->accept(*this);
            if (i<elements.size()-1){
                write(",");
            }
        }
    }
    write("}");
    return true;
}

bool Codegen::visit(const ast::DictLiteral& node) { return true; }

bool Codegen::visit(const ast::ListOrDictAccess& node) {
    node.container()->accept(*this);
    write(".__getitem__(");
    node.keyOrIndex()[0]->accept(*this);
    if(node.keyOrIndex().size()==2){
        write(",");
        node.keyOrIndex()[1]->accept(*this);
    }
    write(")");
    return true;
}

bool Codegen::visit(const ast::BinaryOperation& node) {
    if (node.op().keyword == "**") {
        write("_PEREGRINE_POWER(");
        node.left()->accept(*this);
        write(",");
        node.right()->accept(*this);
        write(")");
    } else if (node.op().keyword == "//") {
        write("_PEREGRINE_FLOOR(");
        node.left()->accept(*this);
        write("/");
        node.right()->accept(*this);
        write(")");
    }
    else if(node.token().tkType==tk_in){
        write("(");
        node.right()->accept(*this);
        write(".__contains__(");
        node.left()->accept(*this);
        write("))");
    }
    else if(node.token().tkType==tk_not_in){
        write("(not ");
        node.right()->accept(*this);
        write(".__contains__(");
        node.left()->accept(*this);
        write("))");
    }
     else {
        write("(");
        node.left()->accept(*this);
        write(" " + node.op().keyword + " ");
        node.right()->accept(*this);
        write(")");
    }
    return true;
}

bool Codegen::visit(const ast::PrefixExpression& node) {
    write("(" + node.prefix().keyword + " ");
    node.right()->accept(*this);
    write(")");
    return true;
}
bool Codegen::visit(const ast::PostfixExpression& node) {
    node.left()->accept(*this);
    write(node.postfix().keyword);
    return true;
}
bool Codegen::visit(const ast::FunctionCall& node) {
    node.name()->accept(*this);
    write("(");

    auto args = node.arguments();
    if (args.size()) {
        for (size_t i = 0; i < args.size(); ++i) {
            if (i)
                write(", ");
            args[i]->accept(*this);
        }
    }

    write(")");
    return true;
}

bool Codegen::visit(const ast::ArrowExpression& node) {
    node.owner()->accept(*this);
    write("->");
    node.referenced()->accept(*this);
    return true;
}


bool Codegen::visit(const ast::DotExpression& node) {
    //FIXME: Not very elegent
    if (!is_dot_exp){
        if (node.owner()->type()==ast::KAstIdentifier){
            std::string name = std::dynamic_pointer_cast<ast::IdentifierExpression>(node.owner())->value();
            if(std::count(enum_name.begin(), enum_name.end(), name)){
                write(name+"___");
                node.referenced()->accept(*this);
            }
            else{
                is_dot_exp=true;
                node.owner()->accept(*this);
                write(".");
                node.referenced()->accept(*this);
            }
        }
        else {
            if(node.owner()->type()!=ast::KAstDotExpression){is_dot_exp=true;}
            node.owner()->accept(*this);
            write(".");
            node.referenced()->accept(*this);
        }
        is_dot_exp=false;
    }
    else{
        node.owner()->accept(*this);
        write(".");
        node.referenced()->accept(*this);
    }
    return true;
}

bool Codegen::visit(const ast::IdentifierExpression& node) {
    if (is_enum){
        write(enum_name.back()+"___");
    }
    write(node.value());
    return true;
}

bool Codegen::visit(const ast::TypeExpression& node) {
    write(node.value());
    return true;
}

bool Codegen::visit(const ast::ListTypeExpr& node) { return true; }

bool Codegen::visit(const ast::DictTypeExpr& node) { return true; }

bool Codegen::visit(const ast::FunctionTypeExpr& node) {
    write("std::function<");
    if (node.returnTypes().size() == 1) {
        node.returnTypes()[0]->accept(*this);
        write("(");
    } else {
        write("void(");
    }
    auto argTypes = node.argTypes();
    if (argTypes.size() > 0) {
        for (size_t i = 0; i < argTypes.size(); ++i) {
            if (i)
                write(",");
            argTypes[i]->accept(*this);
        }
    }
    if(node.returnTypes().size()>1){
        if(node.argTypes().size()>0){
            write(",");
        }
        for(size_t i=0;i<node.returnTypes().size();++i){
            node.returnTypes()[i]->accept(*this);
            write("*");
            if(i<node.returnTypes().size()-1){
                write(",");
            }
        }
    }
    write(")>");
    return true;
}

bool Codegen::visit(const ast::NoLiteral& node) {
    // nothing ig
    return true;
}

bool Codegen::visit(const ast::IntegerLiteral& node) {
    write(node.value());
    return true;
}

bool Codegen::visit(const ast::DecimalLiteral& node) {
    write(node.value());
    return true;
}

bool Codegen::visit(const ast::StringLiteral& node) {
    write("\"" + node.value() + "\"");
    return true;
}

bool Codegen::visit(const ast::BoolLiteral& node) {
    write((node.value() == "True") ? "true" : "false");
    return true;
}

bool Codegen::visit(const ast::NoneLiteral& node) {
    write("NULL");
    return true;
}
bool Codegen::visit(const ast::AssertStatement& node){
    write("if(not ");
    node.condition()->accept(*this);
    write("){\n");
    write("printf(\"AssertionError : in line "+std::to_string(node.token().line)+" in file "+m_filename+"\\n   "+node.token().statement+"\\n\");fflush(stdout);throw error___AssertionError;");
    write("\n}");
    return true;
}
bool Codegen::visit(const ast::StaticStatement& node){
    write("static ");
    node.body()->accept(*this);
    return true;
}
bool Codegen::visit(const ast::InlineStatement& node){
    write("inline ");
    node.body()->accept(*this);
    return true;
}
bool Codegen::visit(const ast::RaiseStatement& node){
    write("throw ");
    if(node.value()->type()!=ast::KAstNoLiteral){
        node.value()->accept(*this);
    }
    else{
        write("0");
    }
    return true;
}
bool Codegen::visit(const ast::UnionLiteral& node){
    write("typedef union{\n");
    for (auto& element:node.elements()){
        element.first->accept(*this);
        write(" ");
        element.second->accept(*this);
        write(";\n");
    }
    write("\n}");
    node.name()->accept(*this);
    return true;
}
bool Codegen::visit(const ast::EnumLiteral& node){
    write("typedef enum{\n");
    auto fields=node.fields();
    std::string name=std::dynamic_pointer_cast<ast::IdentifierExpression>(node.name())->value();
    //TODO:DO something for local enum
    enum_name.push_back(name);
    for (size_t i=0;i<fields.size();++i){
        auto field=fields[i];
        write(name+"___");
        field.first->accept(*this);
        is_enum=true;
        if (field.second->type()!=ast::KAstNoLiteral){
            write(" = ");
            field.second->accept(*this);
        }
        is_enum=false;
        if (i!=fields.size()-1){
            write(",\n");
        }
    }
    write("\n}");
    write(name);
    return true;
}
bool Codegen::visit(const ast::CastStatement& node){
    write("(");
    node.cast_type()->accept(*this);
    write(")(");
    node.value()->accept(*this);
    write(")");
    return true;
}
bool Codegen::visit(const ast::PointerTypeExpr& node){
    node.baseType()->accept(*this);
    write("*");
    return true;
}
bool Codegen::visit(const ast::ClassDefinition& node){
    write("class ");
    node.name()->accept(*this);
    auto name =
        std::dynamic_pointer_cast<ast::IdentifierExpression>(node.name())
            ->value();
    auto parents=node.parent();
    if (parents.size()!=0){
        write(":");
    }
    for (size_t i=0;i<parents.size();++i){
        write("public ");
        parents[i]->accept(*this);
        if(i<parents.size()-1){write(",");}
    }
    write("\n{");
    for (auto& x : node.other()){
        x->accept(*this);
        write(";\n");
    }
    write("public:\n");
    if (!is_class){
        is_class=true;
        for (auto& x : node.attributes()){
            x->accept(*this);
            write(";\n");
        }
        //TODO: Implement this
        for (auto& x : node.methods()){
            magic_methord(x,name);
            write(";\n");
        }
        is_class=false;
    }
    else{
        is_class=true;
        for (auto& x : node.attributes()){
            x->accept(*this);
            write(";\n");
        }
        //TODO: Implement this
        for (auto& x : node.methods()){
            magic_methord(x,name);
            write(";\n");
        }
        is_class=false;
    }
    write("\n}");
    return true;
}
bool Codegen::visit(const ast::WithStatement& node) {
    write("{\n");
    std::vector<ast::AstNodePtr> variables=node.variables();
    std::vector<ast::AstNodePtr> values=node.values();
    std::vector<std::string> no_var;
    for(size_t i=0;i<values.size();++i){
        write("auto CONTEXT____MANAGER____PEREGRINE____");
        no_var.push_back(std::to_string(i));
        write(std::to_string(i));
        write("=");
        values[i]->accept(*this);
        write(";\n");
        if(variables[i]->type()!=ast::KAstNoLiteral){
            write("auto ");
            variables[i]->accept(*this);
            write("=");
            write("CONTEXT____MANAGER____PEREGRINE____"+no_var.back());
            write(".__enter__()");
        }
        else{
            write("CONTEXT____MANAGER____PEREGRINE____"+no_var.back());
            write(".__enter__()");
        }
        write(";\n");
    }
    node.body()->accept(*this);
    for(auto& x:no_var){
        write("CONTEXT____MANAGER____PEREGRINE____"+x);
        write(".__end__();\n");
    }
    write("\n}\n");
    return true;
}
bool Codegen::visit(const ast::DefaultArg& node){
    //TODO:
    // write(".");
    // node.name()->accept(*this);
    // write("=");
    node.value()->accept(*this);
    return true;
}
bool Codegen::visit(const ast::ExportStatement& node){
    //dont mangle this name
    write("extern \"C\" ");
    node.body()->accept(*this);
    return true;
}
bool Codegen::visit(const ast::TernaryIf& node){
    write("(");
    node.if_condition()->accept(*this);
    write(")?");
    node.if_value()->accept(*this);
    write(":");
    node.else_value()->accept(*this);
    return true;
}
bool Codegen::visit(const ast::TryExcept& node){
    write("try{\n");
    node.body()->accept(*this);
    //TODO:This should be base exception
    write("}\ncatch(error __PEREGRINE__exception){\n");
    if(node.except_clauses().size()>0){
        write("if (");
        auto x=node.except_clauses()[0];
        for (size_t i=0;i<x.first.first.size();++i){
            write("__PEREGRINE__exception==");
            x.first.first[i]->accept(*this);
            if(i<x.first.first.size()-1){write(" or ");}
        }
        write("){\n");
        if(x.first.second->type()!=ast::KAstNoLiteral){
            write("auto ");
            x.first.second->accept(*this);
            write("=__PEREGRINE__exception;\n");
        }
        x.second->accept(*this);
        write("}\n");
        for(size_t i=1;i<node.except_clauses().size();++i){
            write("else if (");
            auto x=node.except_clauses()[i];
            for (size_t i=0;i<x.first.first.size();++i){
                write("__PEREGRINE__exception==");
                x.first.first[i]->accept(*this);
                if(i<x.first.first.size()-1){write(" or ");}
            }
            write("){\n");
            if(x.first.second->type()!=ast::KAstNoLiteral){
                write("auto ");
                x.first.second->accept(*this);
                write("=__PEREGRINE__exception;\n");
            }
            x.second->accept(*this);
            write("}\n");
        }
    }
    if(node.else_body()->type()!=ast::KAstNoLiteral){
        if(node.except_clauses().size()>0){
            write("else{");
            node.else_body()->accept(*this);
            write("}\n");
        }
        else{
            node.else_body()->accept(*this);
        }
    }
    else{
        if(node.except_clauses().size()>0){
            write("else{");
            write("throw __PEREGRINE__exception;\n");
            write("}\n");
        }
        else{
            write("throw __PEREGRINE__exception;\n");
        }
    }
    write("}");
    return true; 
}
} // namespace cpp

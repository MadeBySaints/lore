"""Read Python source on stdin and emit declarations without executing it."""
import ast
import json
import sys


class Declarations(ast.NodeVisitor):
    def __init__(self):
        self.scope = []
        self.functions = []

    def visit_ClassDef(self, node):
        self.scope.append(node.name)
        self.generic_visit(node)
        self.scope.pop()

    def visit_FunctionDef(self, node):
        self.scope.append(node.name)
        self.functions.append(dict(
            name=node.name, qualified='.'.join(self.scope),
            line=node.lineno, endLine=node.end_lineno,
            summary=(ast.get_docstring(node) or 'Python declaration.')[:1400],
        ))
        self.generic_visit(node)
        self.scope.pop()

    visit_AsyncFunctionDef = visit_FunctionDef


try:
    module = ast.parse(sys.stdin.read())
except (SyntaxError, ValueError, RecursionError):
    print(json.dumps({'syntaxError': True}))
else:
    declarations = Declarations()
    declarations.visit(module)
    print(json.dumps({'functions': declarations.functions}))

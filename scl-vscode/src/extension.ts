import * as vscode from 'vscode';
import * as cp from 'child_process';
import * as path from 'path';

// Diagnostics collection for compiler errors
let diagnosticCollection: vscode.DiagnosticCollection;

// Built-in function documentation
const builtinDocs: { [key: string]: string } = {
    'draw': 'draw(x, y, height, sprite)\n\nDraws a sprite at position (x, y) with the specified height.\nSets VF to 1 if any pixels are erased (collision).',
    'clear': 'clear;\n\nClears the display (all pixels set to 0).',
    'beep': 'beep(duration)\n\nPlays a sound for the specified duration (in 60ths of a second).',
    'wait': 'wait(duration)\n\nWaits for the specified duration (in 60ths of a second).',
    'key': 'key(n)\n\nReturns 1 if key n (0-F) is currently pressed, 0 otherwise.',
    'waitkey': 'waitkey()\n\nBlocks until a key is pressed, then returns the key number (0-F).',
    'rand': 'rand(max)\n\nReturns a random number from 0 to max (inclusive).',
    'collision': 'collision\n\nReturns VF value from the last draw operation (1 if collision, 0 otherwise).',
    'timer': 'timer\n\nReturns the current value of the delay timer.',
    'drawnum': 'drawnum(value, x, y)\n\nDraws a 3-digit decimal number at position (x, y).',
    'sprite': 'sprite name[height] = { bytes... };\n\nDefines a sprite with the given height and byte data.',
    'byte': 'byte name;\nbyte name = value;\n\nDeclares a local variable (stored in V1-VE registers).\nMax 14 locals per function.',
    'const': 'const NAME = value;\n\nDefines a compile-time constant.',
    'enum': 'enum Name { A = 0, B = 1, ... };\n\nDefines an enumeration of compile-time constants.',
    'global': 'global byte name;\n\nDeclares a global variable (stored in memory).',
    'asm': 'asm { 0xNNNN, ... };\n\nInserts raw CHIP-8 opcodes into the output.',
    'void': 'void function_name() { ... }\n\nDeclares a function that does not return a value.',
    'for': 'for (init; condition; increment) { ... }\n\nStandard for loop.',
    'while': 'while (condition) { ... }\n\nStandard while loop.',
    'if': 'if (condition) { ... } else { ... }\n\nConditional statement.',
    'return': 'return;\nreturn value;\n\nReturns from the current function, optionally with a value.',
    'break': 'break;\n\nExits the current loop.',
    'continue': 'continue;\n\nSkips to the next iteration of the current loop.',
    'entity': 'entity Name { byte x; byte y; ... }\n\nDefines a struct-like type for grouping related data.\nInstances are stored in memory.',
    'switch': 'switch (value) { case N: ... default: ... }\n\nMulti-way branch based on value.\nCases fall through by default.'
};

// Symbol information parsed from source
interface SymbolInfo {
    name: string;
    kind: 'function' | 'sprite' | 'const' | 'enum' | 'global' | 'variable' | 'entity';
    line: number;
    doc?: string;
}

let documentSymbols: Map<string, SymbolInfo[]> = new Map();

export function activate(context: vscode.ExtensionContext) {
    console.log('SCL Language extension activated');

    // Create diagnostics collection
    diagnosticCollection = vscode.languages.createDiagnosticCollection('scl');
    context.subscriptions.push(diagnosticCollection);

    // Register document change handler for diagnostics
    context.subscriptions.push(
        vscode.workspace.onDidSaveTextDocument(doc => {
            if (doc.languageId === 'scl') {
                updateDiagnostics(doc);
                parseSymbols(doc);
            }
        })
    );

    context.subscriptions.push(
        vscode.workspace.onDidOpenTextDocument(doc => {
            if (doc.languageId === 'scl') {
                updateDiagnostics(doc);
                parseSymbols(doc);
            }
        })
    );

    // Initial diagnostics for open SCL files
    vscode.workspace.textDocuments.forEach(doc => {
        if (doc.languageId === 'scl') {
            updateDiagnostics(doc);
            parseSymbols(doc);
        }
    });

    // Register hover provider
    context.subscriptions.push(
        vscode.languages.registerHoverProvider('scl', {
            provideHover(document, position) {
                const range = document.getWordRangeAtPosition(position);
                if (!range) return null;

                const word = document.getText(range);

                // Check built-in docs
                if (builtinDocs[word]) {
                    return new vscode.Hover(
                        new vscode.MarkdownString('```scl\n' + builtinDocs[word] + '\n```')
                    );
                }

                // Check document symbols
                const symbols = documentSymbols.get(document.uri.toString()) || [];
                const symbol = symbols.find(s => s.name === word);
                if (symbol) {
                    let doc = `**${symbol.kind}** \`${symbol.name}\``;
                    if (symbol.doc) {
                        doc += '\n\n' + symbol.doc;
                    }
                    return new vscode.Hover(new vscode.MarkdownString(doc));
                }

                return null;
            }
        })
    );

    // Register definition provider
    context.subscriptions.push(
        vscode.languages.registerDefinitionProvider('scl', {
            provideDefinition(document, position) {
                const range = document.getWordRangeAtPosition(position);
                if (!range) return null;

                const word = document.getText(range);

                // Check document symbols
                const symbols = documentSymbols.get(document.uri.toString()) || [];
                const symbol = symbols.find(s => s.name === word);
                if (symbol) {
                    return new vscode.Location(
                        document.uri,
                        new vscode.Position(symbol.line, 0)
                    );
                }

                return null;
            }
        })
    );

    // Register completion provider
    context.subscriptions.push(
        vscode.languages.registerCompletionItemProvider('scl', {
            provideCompletionItems(document, position) {
                const completions: vscode.CompletionItem[] = [];

                // Add built-in keywords and functions
                const keywords = [
                    'void', 'byte', 'if', 'else', 'while', 'for', 'return',
                    'draw', 'clear', 'wait', 'key', 'waitkey', 'sprite',
                    'rand', 'beep', 'collision', 'break', 'continue', 'timer',
                    'drawnum', 'const', 'asm', 'global', 'enum', 'entity', 'switch', 'case', 'default'
                ];

                keywords.forEach(kw => {
                    const item = new vscode.CompletionItem(kw, vscode.CompletionItemKind.Keyword);
                    if (builtinDocs[kw]) {
                        item.documentation = new vscode.MarkdownString(builtinDocs[kw]);
                    }
                    completions.push(item);
                });

                // Add document symbols
                const symbols = documentSymbols.get(document.uri.toString()) || [];
                symbols.forEach(sym => {
                    const kind = sym.kind === 'function' ? vscode.CompletionItemKind.Function :
                                 sym.kind === 'sprite' ? vscode.CompletionItemKind.Constant :
                                 sym.kind === 'const' || sym.kind === 'enum' ? vscode.CompletionItemKind.Constant :
                                 vscode.CompletionItemKind.Variable;
                    const item = new vscode.CompletionItem(sym.name, kind);
                    if (sym.doc) {
                        item.documentation = new vscode.MarkdownString(sym.doc);
                    }
                    completions.push(item);
                });

                return completions;
            }
        })
    );
}

function parseSymbols(document: vscode.TextDocument) {
    const symbols: SymbolInfo[] = [];
    const text = document.getText();
    const lines = text.split('\n');

    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];

        // Function: void name() or byte name()
        const funcMatch = line.match(/^\s*(void|byte)\s+(\w+)\s*\(/);
        if (funcMatch) {
            symbols.push({
                name: funcMatch[2],
                kind: 'function',
                line: i,
                doc: `Function defined at line ${i + 1}`
            });
        }

        // Sprite: sprite name[height] =
        const spriteMatch = line.match(/^\s*sprite\s+(\w+)\s*\[/);
        if (spriteMatch) {
            symbols.push({
                name: spriteMatch[1],
                kind: 'sprite',
                line: i,
                doc: `Sprite defined at line ${i + 1}`
            });
        }

        // Const: const NAME =
        const constMatch = line.match(/^\s*const\s+(\w+)\s*=/);
        if (constMatch) {
            symbols.push({
                name: constMatch[1],
                kind: 'const',
                line: i,
                doc: `Constant defined at line ${i + 1}`
            });
        }

        // Enum: enum Name { ... }
        const enumMatch = line.match(/^\s*enum\s+(\w+)\s*\{/);
        if (enumMatch) {
            symbols.push({
                name: enumMatch[1],
                kind: 'enum',
                line: i,
                doc: `Enumeration defined at line ${i + 1}`
            });
            
            // Parse enum values
            const enumContent = text.slice(text.indexOf('{', line.length * i));
            const valuesMatch = enumContent.match(/(\w+)\s*=/g);
            if (valuesMatch) {
                valuesMatch.forEach(v => {
                    const name = v.replace(/\s*=/, '');
                    symbols.push({
                        name: name,
                        kind: 'const',
                        line: i,
                        doc: `Enum value in ${enumMatch[1]}`
                    });
                });
            }
        }

        // Global: global byte name
        const globalMatch = line.match(/^\s*global\s+byte\s+(\w+)/);
        if (globalMatch) {
            symbols.push({
                name: globalMatch[1],
                kind: 'global',
                line: i,
                doc: `Global variable defined at line ${i + 1}`
            });
        }

        // Entity type: entity Name { ... }
        const entityMatch = line.match(/^\s*entity\s+(\w+)\s*\{/);
        if (entityMatch) {
            symbols.push({
                name: entityMatch[1],
                kind: 'entity',
                line: i,
                doc: `Entity type defined at line ${i + 1}`
            });
        }

        // Entity instance: EntityType varname; or EntityType varname[n];
        const entityInstMatch = line.match(/^\s*([A-Z]\w*)\s+(\w+)\s*(?:\[|;)/);
        if (entityInstMatch && !['CHIP8', 'ROM'].includes(entityInstMatch[1])) {
            // Check if this looks like an entity type (starts with uppercase)
            const existingEntity = symbols.find(s => s.name === entityInstMatch[1] && s.kind === 'entity');
            if (existingEntity) {
                symbols.push({
                    name: entityInstMatch[2],
                    kind: 'variable',
                    line: i,
                    doc: `${entityInstMatch[1]} instance defined at line ${i + 1}`
                });
            }
        }
    }

    documentSymbols.set(document.uri.toString(), symbols);
}

function updateDiagnostics(document: vscode.TextDocument) {
    const config = vscode.workspace.getConfiguration('scl');
    const compilerPath = config.get<string>('compilerPath', 'scl2.0');

    // Try to compile and capture errors
    const tempOutput = path.join(path.dirname(document.uri.fsPath), '.scl_temp.ch8');
    
    const args = ['compile', document.uri.fsPath, tempOutput];

    cp.exec(`"${compilerPath}" ${args.join(' ')}`, (error, stdout, stderr) => {
        const diagnostics: vscode.Diagnostic[] = [];

        if (error) {
            // Parse error messages from stderr or stdout
            const errorOutput = stderr || stdout;
            const lines = errorOutput.split('\n');

            for (const line of lines) {
                // Parse new error format: "filename:line:column: error: message"
                const newFormatMatch = line.match(/^(?:([^:]+):)?(\d+):(\d+):\s*(error|warning):\s*(.+)$/i);
                if (newFormatMatch) {
                    const lineNum = parseInt(newFormatMatch[2]) - 1;
                    const col = parseInt(newFormatMatch[3]) - 1;
                    const severity = newFormatMatch[4].toLowerCase() === 'warning' 
                        ? vscode.DiagnosticSeverity.Warning 
                        : vscode.DiagnosticSeverity.Error;
                    const message = newFormatMatch[5].trim();

                    const range = new vscode.Range(
                        Math.max(0, lineNum), Math.max(0, col),
                        Math.max(0, lineNum), Number.MAX_VALUE
                    );

                    diagnostics.push(new vscode.Diagnostic(range, message, severity));
                    continue;
                }

                // Parse old error format: "error: message" with optional line info
                const oldMatch = line.match(/(?:error|Error).*?(?:line\s*)?(\d+)(?::(\d+))?.*?:\s*(.+)/i);
                if (oldMatch) {
                    const lineNum = parseInt(oldMatch[1]) - 1;
                    const col = oldMatch[2] ? parseInt(oldMatch[2]) - 1 : 0;
                    const message = oldMatch[3].trim();

                    const range = new vscode.Range(
                        Math.max(0, lineNum), col,
                        Math.max(0, lineNum), Number.MAX_VALUE
                    );

                    diagnostics.push(new vscode.Diagnostic(
                        range,
                        message,
                        vscode.DiagnosticSeverity.Error
                    ));
                } else if (line.includes('error') || line.includes('Error')) {
                    // Generic error without line number
                    diagnostics.push(new vscode.Diagnostic(
                        new vscode.Range(0, 0, 0, 0),
                        line.trim(),
                        vscode.DiagnosticSeverity.Error
                    ));
                } else if (line.includes('warning') || line.includes('Warning')) {
                    // Generic warning without line number
                    diagnostics.push(new vscode.Diagnostic(
                        new vscode.Range(0, 0, 0, 0),
                        line.trim(),
                        vscode.DiagnosticSeverity.Warning
                    ));
                }
            }
        }

        // Clean up temp file
        try {
            require('fs').unlinkSync(tempOutput);
        } catch (e) {
            // Ignore cleanup errors
        }

        diagnosticCollection.set(document.uri, diagnostics);
    });
}

export function deactivate() {
    if (diagnosticCollection) {
        diagnosticCollection.dispose();
    }
}

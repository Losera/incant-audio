# Architecture

## Pipeline

```
User prompt (natural language)
        |
        v
LLM engine (Claude / GPT)
  - system prompt enforces Faust-only output
  - few-shot examples in prompts/system_prompt.txt
        |
        v
Faust DSL string
  - validated via: faust -lang cpp <file>.dsp -o /dev/null
  - retry loop feeds compiler stderr back to LLM
        |
        v
libfaust LLVM JIT  (FaustEngine.cpp inside JUCE host)
  - compiles on background thread
  - atomic pointer swap onto audio thread
        |
        v
VST3 / AU host plugin running in DAW
```

## Key files

| File | Role |
|------|------|
| host/Source/FaustEngine.h | libfaust JIT wrapper |
| host/Source/ParamPool.h   | 64-slot pre-allocated parameter pool |
| llm/generate.py           | LLM call + retry loop |
| llm/prompts/system_prompt.txt | Few-shot Faust generation prompt |

#include "slate.hpp"


void interpreter_grow_stack(struct object_heap* oh, struct Interpreter* i, word_t minimum) {

  struct OopArray * newStack;
  
  /* i suppose this isn't treated as a SmallInt type*/
  do {
    i->stackSize *= 2;
  } while (i->stackSize < minimum);
  newStack = (struct OopArray *) heap_clone_oop_array_sized(oh, get_special(oh, SPECIAL_OOP_ARRAY_PROTO), i->stackSize);
  copy_words_into((word_t*) i->stack->elements, i->stackPointer, (word_t*) newStack->elements);
  
  i -> stack = newStack;

}

void interpreter_stack_allocate(struct object_heap* oh, struct Interpreter* i, word_t n) {

  if (i->stackPointer + n > i->stackSize) {
    interpreter_grow_stack(oh, i, i->stackPointer + n);
    assert(i->stackPointer + n <= i->stackSize);
  }
  i->stackPointer += n;

#ifdef PRINT_DEBUG_STACK_PUSH
  fprintf(stderr, "stack allocate, new stack pointer: %" PRIdPTR "\n", i->stackPointer);
#endif

}

void interpreter_stack_push(struct object_heap* oh, struct Interpreter* i, struct Object* value) {

#ifdef PRINT_DEBUG_STACK_PUSH
  fprintf(stderr, "Stack push at %" PRIdPTR " (fp=%" PRIdPTR "): ", i->stackPointer, i->framePointer); print_type(oh, value);
#endif
  if (!object_is_smallint(value)) {
    assert(object_hash(value) < ID_HASH_RESERVED); /*catch gc bugs earlier*/
  }
  if (i->stackPointer == i->stackSize) {
    interpreter_grow_stack(oh, i, 0);
  }
  heap_store_into(oh, (struct Object*)i->stack, value);
  i->stack->elements[i -> stackPointer] = value;
  i->stackPointer++;
}

struct Object* interpreter_stack_pop(struct object_heap* oh, struct Interpreter* i) {

  if (i -> stackPointer == 0) {
    /*error("Attempted to pop empty interpreter stack.");*/
    assert(0);
  }

  i->stackPointer = i->stackPointer - 1;

  {
    struct Object* o = i->stack->elements[i->stackPointer];
#ifdef PRINT_DEBUG_STACK_POINTER
    fprintf(stderr, "popping from stack, new stack pointer: %" PRIdPTR "\n", i->stackPointer);
    /*print_detail(oh, o);*/
#endif
    return o;
  }

}

void interpreter_stack_pop_amount(struct object_heap* oh, struct Interpreter* i, word_t amount) {

  if (i -> stackPointer - amount < 0) {
    /*error("Attempted to pop empty interpreter stack.");*/
    assert(0);
  }

  i->stackPointer = i->stackPointer - amount;

}


void unhandled_signal(struct object_heap* oh, struct Symbol* selector, word_t n, struct Object* args[]) {
  word_t i;
  fprintf(stderr, "Unhandled signal: "); print_symbol(selector); fprintf(stderr, " with %" PRIdPTR " arguments: \n", n);
  for (i = 0; i<n; i++) {
    fprintf(stderr, "arg[%" PRIdPTR "] = ", i);
    print_detail(oh, args[i]);
  }
  fprintf(stderr, "partial stack: \n");
  /*print_stack_types(oh, 200);*/
  print_backtrace(oh);
  assert(0);
  exit(1);

}


void interpreter_signal(struct object_heap* oh, struct Interpreter* i, struct Object* signal, struct Object* args[], word_t n, struct Object* opts[], word_t optCount, word_t resultStackPointer) {
  
  struct Closure* method;
  struct Symbol* selector = (struct Symbol*)signal;
  struct MethodDefinition* def = method_dispatch_on(oh, selector, args, n, NULL);
  if (def == NULL) {
    unhandled_signal(oh, selector, n, args);
  }
  /*take this out when the debugger is mature*/
  /*  print_backtrace(oh);
      assert(0);*/
  method = (struct Closure*)def->method;
  interpreter_apply_to_arity_with_optionals(oh, i, method, args, n, opts, optCount, resultStackPointer);
}

void interpreter_signal_with(struct object_heap* oh, struct Interpreter* i, struct Object* signal, struct Object* arg, struct Object* opts[], word_t optCount, word_t resultStackPointer) {

  struct Object* args[1];
  args[0] = arg;
  interpreter_signal(oh, i, signal, args, 1, opts, optCount, resultStackPointer);
}


void interpreter_signal_with_with(struct object_heap* oh, struct Interpreter* i, struct Object* signal, struct Object* arg, struct Object* arg2, struct Object* opts[], word_t optCount, word_t resultStackPointer) {
  struct Object* args[2];
  args[0] = arg;
  args[1] = arg2;
  interpreter_signal(oh, i, signal, args, 2, opts, optCount, resultStackPointer);
}

void interpreter_signal_with_with_with(struct object_heap* oh, struct Interpreter* i, struct Object* signal, struct Object* arg, struct Object* arg2, struct Object* arg3, struct Object* opts[], word_t optCount, word_t resultStackPointer) {
  struct Object* args[3];
  args[0] = arg;
  args[1] = arg2;
  args[2] = arg3;
  interpreter_signal(oh, i, signal, args, 3, opts, optCount, resultStackPointer);
}

bool_t interpreter_dispatch_optional_keyword(struct object_heap* oh, struct Interpreter * i, struct Object* key, struct Object* value) {

  struct OopArray* optKeys = i->method->optionalKeywords;

  word_t optKey, size = array_size(optKeys);

  for (optKey = 0; optKey < size; optKey++) {
    if (optKeys->elements[optKey] == key) {
      if (i->method->heapAllocate == oh->cached.true_object) {
        heap_store_into(oh, (struct Object*)i->lexicalContext, value);
        i->lexicalContext->variables[object_to_smallint(i->method->inputVariables) + optKey] = value;
      } else {
        heap_store_into(oh, (struct Object*)i->stack, value);
        i->stack->elements[i->framePointer + object_to_smallint(i->method->inputVariables) + optKey] = value;
      }
      return TRUE;
    }
  }

  /*fix*/
  return FALSE;
}

void interpreter_dispatch_optionals(struct object_heap* oh, struct Interpreter * i, struct Object* opts[], word_t optCount) {

  word_t index;
  for (index = 0; index < optCount; index += 2) {
    interpreter_dispatch_optional_keyword(oh, i, opts[index], opts[index+1]);
  }

}


void interpreter_apply_to_arity_with_optionals(struct object_heap* oh, struct Interpreter * i, struct Closure * closure,
                                               struct Object* args[], word_t n, struct Object* opts[], word_t optCount,
                                               word_t resultStackPointer) {


  word_t inputs, framePointer, j, beforeCallStackPointer;
  struct Object** vars;
  Pinned<struct LexicalContext> lexicalContext(oh);
  Pinned<struct CompiledMethod> method(oh);
  

#ifdef PRINT_DEBUG
  fprintf(stderr, "apply to arity %" PRIdPTR "\n", n);
#endif

  struct Object* traitsWindow = closure->base.map->delegates->elements[0];
  if (traitsWindow != oh->cached.compiled_method_window && traitsWindow != oh->cached.closure_method_window) {
    interpreter_signal_with(oh, oh->cached.interpreter, get_special(oh, SPECIAL_OOP_TYPE_ERROR_ON), (struct Object*)closure, NULL, 0, resultStackPointer);
    return;
  }
  method = closure->method;
  inputs = object_to_smallint(method->inputVariables);

  method->callCount = smallint_to_object(object_to_smallint(method->callCount) + 1);


  assert(n <= MAX_ARITY);

#ifndef SLATE_DISABLE_METHOD_OPTIMIZATION
  /* optimize the callee function after a set number of calls*/
  if (oh->automaticallyInline
      && method->callCount > (struct Object*)CALLEE_OPTIMIZE_AFTER
      && method->isInlined == oh->cached.false_object) {
      method_optimize(oh, method);
  }
  if (method->nextInlineAtCallCount != oh->cached.nil &&
      (word_t)method->nextInlineAtCallCount < (word_t)method->callCount) {
    method_optimize(oh, method);
    method->nextInlineAtCallCount = smallint_to_object(object_to_smallint(method->callCount) * 2);

  }
#endif
  
  if (n < inputs || (n > inputs && method->restVariable != oh->cached.true_object)) {
    Pinned<struct OopArray> argsArray(oh);
    argsArray = heap_clone_oop_array_sized(oh, get_special(oh, SPECIAL_OOP_ARRAY_PROTO), n);
    copy_words_into(args, n, argsArray->elements);
    interpreter_signal_with_with(oh, i, get_special(oh, SPECIAL_OOP_WRONG_INPUTS_TO), (struct Object*) argsArray, (struct Object*)method, NULL, 0, resultStackPointer);
    return;
  }


  framePointer = i->stackPointer + FUNCTION_FRAME_SIZE;
  /* we save this so each function call doesn't leak the stack */
  beforeCallStackPointer = i->stackPointer;

  if (method->heapAllocate == oh->cached.true_object) {
    lexicalContext = (struct LexicalContext*) heap_clone_oop_array_sized(oh, get_special(oh, SPECIAL_OOP_LEXICAL_CONTEXT_PROTO), object_to_smallint(method->localVariables));
    lexicalContext->framePointer = smallint_to_object(framePointer);
    interpreter_stack_allocate(oh, i, FUNCTION_FRAME_SIZE + object_to_smallint(method->registerCount));
    vars = &lexicalContext->variables[0];
    for (j = 0; j < inputs; j++) {
      heap_store_into(oh, (struct Object*)lexicalContext, args[j]);
    }

  } else {
    lexicalContext = (struct LexicalContext*) oh->cached.nil;
    interpreter_stack_allocate(oh, i, FUNCTION_FRAME_SIZE /*frame size in words*/ + object_to_smallint(method->localVariables) + object_to_smallint(method->registerCount));
    vars = &i->stack->elements[framePointer];
    for (j = 0; j < inputs; j++) {
      heap_store_into(oh, (struct Object*)i->stack, args[j]);
    }
  }
  //std::vector<Pinned<struct Object> > pinnedVars(n, Pinned<struct Object>(oh));
  //for (word_t k = 0; k < n; k++) pinnedVars[k] = vars[k];


  copy_words_into(args, inputs, vars);
  i->stack->elements[framePointer - FRAME_OFFSET_BEFORE_CALL_STACK_POINTER] = smallint_to_object(beforeCallStackPointer);
  i->stack->elements[framePointer - FRAME_OFFSET_RESULT_STACK_POINTER] = smallint_to_object(resultStackPointer);
  i->stack->elements[framePointer - FRAME_OFFSET_CODE_POINTER] = smallint_to_object(i->codePointer);
  i->stack->elements[framePointer - FRAME_OFFSET_METHOD] = (struct Object*) closure;
  i->stack->elements[framePointer - FRAME_OFFSET_LEXICAL_CONTEXT] = (struct Object*) lexicalContext;
  i->stack->elements[framePointer - FRAME_OFFSET_PREVIOUS_FRAME_POINTER] = smallint_to_object(i->framePointer);

  heap_store_into(oh, (struct Object*)i->stack, (struct Object*)closure);
  heap_store_into(oh, (struct Object*)i->stack, (struct Object*)lexicalContext);

  if (oh->currentlyProfiling) {
    profiler_enter_method(oh, (struct Object*)i->closure, (struct Object*)closure, 1);
  }

  i->framePointer = framePointer;
  i->method = method;
  i->closure = closure;
  i->lexicalContext = lexicalContext;

  heap_store_into(oh, (struct Object*)i, (struct Object*)lexicalContext);
  heap_store_into(oh, (struct Object*)i, (struct Object*)method);
  heap_store_into(oh, (struct Object*)i, (struct Object*)closure);

  i->codeSize = array_size(method->code);
  i->codePointer = 0;
  fill_words_with(((word_t*)vars)+inputs, object_to_smallint(method->localVariables) - inputs, (word_t)oh->cached.nil);

  if (n > inputs) {
    Pinned<struct OopArray> restArgs(oh);
    restArgs = heap_clone_oop_array_sized(oh, get_special(oh, SPECIAL_OOP_ARRAY_PROTO), n - inputs);
    copy_words_into(args+inputs, n - inputs, restArgs->elements);
    vars[inputs+array_size(method->optionalKeywords)] = (struct Object*) restArgs;
    heap_store_into(oh, (struct Object*)lexicalContext, (struct Object*)restArgs);/*fix, not always right*/
  } else {
    if (method->restVariable == oh->cached.true_object) {
      vars[inputs+array_size(method->optionalKeywords)] = get_special(oh, SPECIAL_OOP_ARRAY_PROTO);
    }
  }
  if (opts != NULL && optCount > 0) {
    interpreter_dispatch_optionals(oh, i, opts, optCount);
  }
}


void send_to_through_arity_with_optionals(struct object_heap* oh,
                                          struct Symbol* selector, struct Object* args[],
                                          struct Object* dispatchers[], word_t arity, struct Object* opts[], word_t optCount,
                                          word_t resultStackPointer/*where to put the return value in the stack*/) {
  Pinned<struct OopArray> argsArray(oh);
  Pinned<struct Closure> method(oh);
  Pinned<struct Object> traitsWindow(oh);
  Pinned<struct MethodDefinition> def(oh);
  Pinned<struct CompiledMethod> callerMethod(oh);
  callerMethod = oh->cached.interpreter->method;

  /*make sure they are pinned*/
  assert(arity <= MAX_ARITY);

  def = NULL;

#ifndef SLATE_DISABLE_PIC_LOOKUP
  word_t addToPic = FALSE;
  /* set up a PIC for the caller if it has been called a lot */
  if (object_is_old(oh, (struct Object*)callerMethod)
      && (callerMethod->callCount > (struct Object*)CALLER_PIC_SETUP_AFTER
          || object_is_smallint(callerMethod->nextInlineAtCallCount))) {
    if ((struct Object*)callerMethod->calleeCount == oh->cached.nil) {
      method_pic_setup(oh, callerMethod);
      addToPic = TRUE;
    } else {
      def = method_pic_find_callee(oh, callerMethod, selector, arity, dispatchers);

      if ((struct Object*)def==NULL) {
        addToPic = TRUE;
#ifdef PRINT_DEBUG_PIC_HITS
        fprintf(stderr, "PIC miss\n");
#endif

      }
      else {
#ifdef PRINT_DEBUG_PIC_HITS
        fprintf(stderr, "PIC hit\n");
#endif
      }
    }
    
  }
#endif

  if ((struct Object*)def == NULL) {
    def = method_dispatch_on(oh, selector, dispatchers, arity, NULL);
  } else {
#ifdef PRINT_DEBUG_PIC_HITS
    fprintf(stderr, "Using PIC over dispatch\n");
#endif
  }
  if ((struct Object*)def == NULL) {
    // Export the arguments into the image and pin it:
    argsArray = (struct OopArray*) heap_clone_oop_array_sized(oh, get_special(oh, SPECIAL_OOP_ARRAY_PROTO), arity);
    copy_words_into((word_t*)dispatchers, arity, (word_t*)&argsArray->elements[0]);

    Pinned<struct OopArray> nestedOptsArray(oh);
    struct Object* newOpts[2];
    word_t newOptCount = 0;
    if (optCount > 0) {
      nestedOptsArray = heap_clone_oop_array_sized(oh, get_special(oh, SPECIAL_OOP_ARRAY_PROTO), optCount);
      copy_words_into(opts, optCount, nestedOptsArray->elements);

      newOpts[0] = get_special(oh, SPECIAL_OOP_OPTIONALS);
      newOpts[1] = (struct Object*)nestedOptsArray;
      newOptCount = 2;
    }

    // Signal notFoundOn:
    interpreter_signal_with_with(oh, oh->cached.interpreter, get_special(oh, SPECIAL_OOP_NOT_FOUND_ON), (struct Object*)selector, (struct Object*)argsArray, newOpts, newOptCount, resultStackPointer);

    return;
  }

  /*PIC add here*/
#ifndef SLATE_DISABLE_PIC_LOOKUP
  if (addToPic) method_pic_add_callee(oh, callerMethod, def, arity, dispatchers);
#endif

  method = (struct Closure*)def->method;
  traitsWindow = method->base.map->delegates->elements[0]; /*fix should this location be hardcoded as the first element?*/
  if (traitsWindow == oh->cached.primitive_method_window) {
#ifdef PRINT_DEBUG
    fprintf(stderr, "calling primitive: %" PRIdPTR "\n", object_to_smallint(((struct PrimitiveMethod*)method)->index));
#endif
    //sometimes primitives call sent_to or apply_to which can screw up the call stack. i won't measure them for now
    
    Pinned<struct OopArray> pinnedStack(oh);
    pinnedStack = oh->cached.interpreter->stack;
    primitives[object_to_smallint(((struct PrimitiveMethod*)method)->index)](oh, args, arity, opts, optCount, resultStackPointer);
  } else if (traitsWindow == oh->cached.compiled_method_window || traitsWindow == oh->cached.closure_method_window) {
    interpreter_apply_to_arity_with_optionals(oh, oh->cached.interpreter, method, args, arity, opts, optCount, resultStackPointer);
  } else {
    argsArray = (struct OopArray*) heap_clone_oop_array_sized(oh, get_special(oh, SPECIAL_OOP_ARRAY_PROTO), arity);
    copy_words_into((word_t*)dispatchers, arity, (word_t*)&argsArray->elements[0]);

    Pinned<struct OopArray> nestedOptsArray(oh);
    struct Object* newOpts[2];
    word_t newOptCount = 0;
    if (optCount > 0) {
      nestedOptsArray = heap_clone_oop_array_sized(oh, get_special(oh, SPECIAL_OOP_ARRAY_PROTO), optCount);
      copy_words_into(opts, optCount, nestedOptsArray->elements);

      newOpts[0] = get_special(oh, SPECIAL_OOP_OPTIONALS);
      newOpts[1] = (struct Object*)nestedOptsArray;
      newOptCount = 2;
    }

    interpreter_signal_with_with(oh, oh->cached.interpreter, get_special(oh, SPECIAL_OOP_APPLY_TO), def->method, (struct Object*)argsArray, newOpts, newOptCount, resultStackPointer);
  }

}


bool_t interpreter_return_result(struct object_heap* oh, struct Interpreter* i, word_t context_depth, struct Object* result, word_t prevCodePointer) {
  /* Implements a non-local return with a value, specifying the block
   * to return from via lexical offset. Returns a success Boolean. The
   * prevCodePointer is the location of the return instruction that
   * returned. In the case that we have an ensure handler to run, we
   * will use that code pointer so the return is executed again.
   */
  word_t framePointer;
  word_t ensureHandlers;
  word_t resultStackPointer;

#ifdef PRINT_DEBUG_FUNCALL
      fprintf(stderr, "interpreter_return_result BEFORE\n");
      fprintf(stderr, "stack pointer: %" PRIdPTR "\n", i->stackPointer);
      fprintf(stderr, "frame pointer: %" PRIdPTR "\n", i->framePointer);
      print_stack_types(oh, MAX_ARITY);
#endif


  if (context_depth == 0) {
    framePointer = i->framePointer;
  } else {
    struct LexicalContext* targetContext = i->closure->lexicalWindow[context_depth-1];
    framePointer = object_to_smallint(targetContext->framePointer);
    if (framePointer > i->stackPointer || (struct Object*)targetContext != i->stack->elements[framePointer - FRAME_OFFSET_LEXICAL_CONTEXT]) {
      resultStackPointer = (word_t)i->stack->elements[framePointer - FRAME_OFFSET_RESULT_STACK_POINTER]>>1;
      interpreter_signal_with_with(oh, i, get_special(oh, SPECIAL_OOP_MAY_NOT_RETURN_TO),
                                   (struct Object*)i->closure, (struct Object*) targetContext, NULL, 0, resultStackPointer);
      return 1;
    }
  }

  resultStackPointer = (word_t)i->stack->elements[framePointer - FRAME_OFFSET_RESULT_STACK_POINTER]>>1;

  /*store the result before we get interrupted for a possible finalizer... fixme i'm not sure
   if this is right*/
  if (result != NULL) {
#ifdef PRINT_DEBUG_STACK
    fprintf(stderr, "setting stack[%" PRIdPTR "] = ", resultStackPointer); print_object(result);
#endif

    i->stack->elements[resultStackPointer] = result;
    heap_store_into(oh, (struct Object*)i->stack, (struct Object*)result);
  }

  ensureHandlers = object_to_smallint(i->ensureHandlers);
  if (framePointer <= ensureHandlers) {
    Pinned<struct Object> ensureHandler(oh);
    ensureHandler = i->stack->elements[ensureHandlers+1];
#ifdef PRINT_DEBUG_ENSURE
  fprintf(stderr, "current ensure handlers at %" PRIdPTR "\n", object_to_smallint(i->ensureHandlers));
#endif
    assert(object_to_smallint(i->stack->elements[ensureHandlers]) < 0x1000000); /*sanity check*/
    i->ensureHandlers = i->stack->elements[ensureHandlers];
#ifdef PRINT_DEBUG_ENSURE
  fprintf(stderr, "reset ensure handlers at %" PRIdPTR "\n", object_to_smallint(i->ensureHandlers));
#endif

    interpreter_stack_push(oh, i, smallint_to_object(i->stackPointer));
    interpreter_stack_push(oh, i, smallint_to_object(resultStackPointer));
    interpreter_stack_push(oh, i, smallint_to_object(prevCodePointer));
    interpreter_stack_push(oh, i, get_special(oh, SPECIAL_OOP_ENSURE_MARKER));
    interpreter_stack_push(oh, i, oh->cached.nil);
    interpreter_stack_push(oh, i, smallint_to_object(i->framePointer));
    i->codePointer = 0;
    i->framePointer = i->stackPointer;

    // there are three places (that I know of, heh) where a method is put on or off the stack
    // 1. interpreter apply to
    // 2. interpreter return from
    // 3. Here, manually for ensure markers (not sure about the original point of having these)... probably due to the stack based old vm?
    if (oh->currentlyProfiling) {
      profiler_enter_method(oh, (struct Object*)i->closure, (struct Object*)i->stack->elements[i->framePointer - FRAME_OFFSET_METHOD], 1);
    }


    /*assert(0); fixme not sure if this is totally the right way to set up the stack yet*/
    {
      interpreter_apply_to_arity_with_optionals(oh, i, (struct Closure*) ensureHandler, NULL, 0, NULL, 0, resultStackPointer);
    }
    return 1;
  }
  i->stackPointer = object_to_smallint(i->stack->elements[framePointer - FRAME_OFFSET_BEFORE_CALL_STACK_POINTER]);
  i->framePointer = object_to_smallint(i->stack->elements[framePointer - FRAME_OFFSET_PREVIOUS_FRAME_POINTER]);
  if (i->framePointer < FUNCTION_FRAME_SIZE) {
    /* returning from the last function on the stack seems to happen when the user presses Ctrl-D */
    exit(0);
    return 0;
  }

  if (oh->currentlyProfiling) {
    profiler_enter_method(oh, (struct Object*)i->closure, (struct Object*)i->stack->elements[i->framePointer - FRAME_OFFSET_METHOD], 0);
  }


  i->codePointer = object_to_smallint(i->stack->elements[framePointer - FRAME_OFFSET_CODE_POINTER]);
  i->lexicalContext = (struct LexicalContext*) i->stack->elements[i->framePointer - FRAME_OFFSET_LEXICAL_CONTEXT];
  i->closure = (struct Closure*) i->stack->elements[i->framePointer - FRAME_OFFSET_METHOD];
  heap_store_into(oh, (struct Object*)i, (struct Object*)i->lexicalContext);
  heap_store_into(oh, (struct Object*)i, (struct Object*)i->closure);
  i->method = i->closure->method;
  heap_store_into(oh, (struct Object*)i, (struct Object*)i->closure->method);

  i->codeSize = array_size(i->method->code);

#ifdef PRINT_DEBUG_FUNCALL
      fprintf(stderr, "interpreter_return_result AFTER\n");
      fprintf(stderr, "stack pointer: %" PRIdPTR "\n", i->stackPointer);
      fprintf(stderr, "frame pointer: %" PRIdPTR "\n", i->framePointer);
      print_stack_types(oh, MAX_ARITY);
#endif


  return 1;

}





void interpreter_resend_message(struct object_heap* oh, struct Interpreter* i, word_t n, word_t resultStackPointer) {

  word_t framePointer;
  Pinned<struct LexicalContext> lexicalContext(oh);
  Pinned<struct Object> barrier(oh), traitsWindow(oh);
  struct Object **args;
  Pinned<struct Symbol> selector(oh);
  Pinned<struct OopArray> argsArray(oh);
  Pinned<struct Closure> method(oh);
  Pinned<struct CompiledMethod> resender(oh);
  Pinned<struct MethodDefinition> def(oh);

  if (n == 0) {
    framePointer = i->framePointer;
    lexicalContext = i->lexicalContext;
  } else {
    lexicalContext = i->closure->lexicalWindow[n-1];
    framePointer = object_to_smallint(lexicalContext->framePointer);
    /*Attempted to resend without enclosing method definition.*/
    assert((struct Object*)lexicalContext == i->stack->elements[framePointer - FRAME_OFFSET_LEXICAL_CONTEXT]);
  }

  barrier = i->stack->elements[framePointer - FRAME_OFFSET_METHOD];
  resender = ((struct Closure*) barrier)->method;
  args = (resender->heapAllocate == oh->cached.true_object)? lexicalContext->variables : &i->stack->elements[framePointer];


  selector = resender->selector;
  n = object_to_smallint(resender->inputVariables);
  assert(n <= MAX_ARITY);
  std::vector<Pinned<struct Object> > pinnedArgs(n, Pinned<struct Object>(oh));
  for (int k = 0; k < n; k++) pinnedArgs[k] = args[k];

  def = method_dispatch_on(oh, selector, args, n, barrier);
  pinnedArgs[0] = args[0];
  if ((struct Object*)def == NULL) {
    argsArray = heap_clone_oop_array_sized(oh, get_special(oh, SPECIAL_OOP_ARRAY_PROTO), n);
    copy_words_into((word_t*)args, n, (word_t*)argsArray->elements);
    interpreter_signal_with_with_with(oh, i, get_special(oh, SPECIAL_OOP_NOT_FOUND_ON),
                                      (struct Object*)selector, (struct Object*)argsArray, (struct Object*) resender, NULL, 0, resultStackPointer);
    return;

  }

  method = (struct Closure*)def->method;
  traitsWindow = method->base.map->delegates->elements[0];
  if (traitsWindow == oh->cached.primitive_method_window) {
#ifdef PRINT_DEBUG
    fprintf(stderr, "calling primitive: %" PRIdPTR "\n", object_to_smallint(((struct PrimitiveMethod*)method)->index));
#endif
    primitives[object_to_smallint(((struct PrimitiveMethod*)method)->index)](oh, args, n, NULL, 0, resultStackPointer);
    return;
  }

  if (traitsWindow == oh->cached.compiled_method_window || traitsWindow == oh->cached.closure_method_window) {
    Pinned<struct OopArray> optKeys(oh);
    optKeys = resender->optionalKeywords;
    interpreter_apply_to_arity_with_optionals(oh, i, method, args, n, NULL, 0, resultStackPointer);
    if (i->closure == method) {
      word_t optKey;
      for (optKey = 0; optKey < array_size(optKeys); optKey++) {
        struct Object* optVal = args[n+optKey];
        if (optVal != oh->cached.nil) {
          interpreter_dispatch_optional_keyword(oh, i, optKeys->elements[optKey], optVal);
        }
      }
    }

  } else {

    argsArray = (struct OopArray*) heap_clone_oop_array_sized(oh, get_special(oh, SPECIAL_OOP_ARRAY_PROTO), n);
    copy_words_into((word_t*) args, n, (word_t*) argsArray->elements);
    //fixme: maybe lose optionals? not sure how to handle this
    interpreter_signal_with_with(oh, i, get_special(oh, SPECIAL_OOP_APPLY_TO),
                                 def->method, (struct Object*) argsArray, &args[n], array_size(resender->optionalKeywords), resultStackPointer);
    
  }


}



void interpreter_branch_keyed(struct object_heap* oh, struct Interpreter * i, struct OopArray* table, struct Object* oop) {

  word_t tableSize;
  word_t hash;
  word_t index;
  
  if (oop_is_object((word_t)oop))
    hash = object_hash(oop);
  else
    hash = object_to_smallint(oop);
  tableSize = array_size(table);
  hash = hash & (tableSize - 2); /*fix: check this out... size is probably 2^n, so XX & 2^n-2 is like mod tableSize?*/
  index = hash;

  /*help: why isn't this just one loop from 0 to tableSize? how is this opcode used?*/
  while (index < tableSize)
  {
    struct Object* offset;
    struct Object* key;
    
    key = table->elements[index];
    offset = table->elements[index + 1];
    if (offset == oh->cached.nil)
    {
      return;
    }
    if (oop == key)
    {
      i->codePointer = i->codePointer + object_to_smallint(offset); /*fix check that offset is the right type*/
      
      return;
    }
    index = index + 2;
  }
  index = 0;
  while (index < hash)
  {
    struct Object* offset;
    struct Object* key;
    
    key = table->elements[index];
    offset = table->elements[index + 1];
    if (offset == oh->cached.nil)
    {
      return;
    }
    if (oop == key)
    {
      i->codePointer = i->codePointer + object_to_smallint(offset);
      
      return;
    }
    index = index + 2;
  }


}


void interpret(struct object_heap* oh) {

 /* we can set a conditional breakpoint if the vm crash is consistent */
  unsigned long int instruction_counter = 0;
#if SLATE_CUSTOM_BREAKPOINT
  word_t myBreakpoint = 0;
#endif

#ifdef PRINT_DEBUG
  fprintf(stderr, "Interpret: img:%p size:%" PRIdPTR " spec:%p next:%" PRIdPTR "\n",
         (void*)oh->memoryOld, oh->memoryOldSize, (void*)oh->special_objects_oop, oh->lastHash);
  fprintf(stderr, "Special oop: "); print_object((struct Object*)oh->special_objects_oop);
#endif

  cache_specials(oh);
  std::vector<Pinned<struct Object> > pinnedObjects(6, Pinned<struct Object>(oh));
  pinnedObjects[0] = (struct Object*) oh->cached.interpreter;
  pinnedObjects[1] = (struct Object*) oh->cached.true_object;
  pinnedObjects[2] = (struct Object*) oh->cached.false_object;
  pinnedObjects[3] = (struct Object*) oh->cached.primitive_method_window;
  pinnedObjects[4] = (struct Object*) oh->cached.compiled_method_window;
  pinnedObjects[5] = (struct Object*) oh->cached.closure_method_window;

#ifdef PRINT_DEBUG
  fprintf(stderr, "Interpreter stack: "); print_object((struct Object*)oh->cached.interpreter);
  fprintf(stderr, "Interpreter stack size: %" PRIdPTR "\n", oh->cached.interpreter->stackSize);
  fprintf(stderr, "Interpreter stack pointer: %" PRIdPTR "\n", oh->cached.interpreter->stackPointer);
  fprintf(stderr, "Interpreter frame pointer: %" PRIdPTR "\n", oh->cached.interpreter->framePointer);
#endif 
  /*fixme this should only be called in the initial bootstrap because
    the stack doesn't have enough room for the registers */
  if (oh->cached.interpreter->framePointer == FUNCTION_FRAME_SIZE 
      && oh->cached.interpreter->stackPointer == FUNCTION_FRAME_SIZE
      && oh->cached.interpreter->stackSize == MAX_ARITY) {
    interpreter_stack_allocate(oh, oh->cached.interpreter, object_to_smallint(oh->cached.interpreter->method->registerCount));
  }

  for (;;) {
    word_t op, prevPointer;
    struct Interpreter* i;
    cache_specials(oh);
    i = oh->cached.interpreter; /*it won't move while we are in here */
    pinnedObjects[0] = (struct Object*) oh->cached.interpreter;

    /*while (oh->cached.interpreter->codePointer < oh->cached.interpreter->codeSize) {*/
    /*optimize and make sure every function has manual return opcodes*/
    for(;;) {
      
      if (oh->interrupt_flag) {
        oh->interrupt_flag = 0;
        break;
      }
      
      if (globalInterrupt) {
        fprintf(stderr, "\nInterrupting...\n");
        interpreter_signal_with(oh, oh->cached.interpreter, get_special(oh, SPECIAL_OOP_TYPE_ERROR_ON), oh->cached.nil, NULL, 0, object_to_smallint(i->stack->elements[i->framePointer - FRAME_OFFSET_RESULT_STACK_POINTER]));
        globalInterrupt = 0;
      }

#ifdef SLATE_CUSTOM_BREAKPOINT
      if (instruction_counter > 11475843) {
        myBreakpoint++;
      }
#endif

      instruction_counter++;
      prevPointer = i->codePointer;
      op = (word_t)i->method->code->elements[i->codePointer];
#ifdef PRINT_DEBUG_CODE_POINTER
      fprintf(stderr, "(%" PRIdPTR "/%" PRIdPTR ") %" PRIdPTR " ", i->codePointer, i->codeSize, op>>1);
#endif
      i->codePointer++;

      switch (op) {

      case OP_SEND:
        {
          word_t result, arity;
          int k;
          Pinned<struct Object> selector(oh);
          struct Object* argsArray[MAX_ARITY], *pinnedArgs[MAX_ARITY];
          result = SSA_NEXT_PARAM_SMALLINT;
          selector = SSA_NEXT_PARAM_OBJECT;
          arity = SSA_NEXT_PARAM_SMALLINT;


#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "send message fp: %" PRIdPTR ", result: %" PRIdPTR ", arity: %" PRIdPTR ", message: ", i->framePointer, result, arity);
          print_type(oh, selector);
#endif
          assert(arity <= MAX_ARITY);

          HEAP_READ_AND_PIN_ARGS(k, arity, argsArray, pinnedArgs);

          send_to_through_arity_with_optionals(oh, (struct Symbol*)selector, argsArray, argsArray, arity, NULL, 0, i->framePointer + result);
          
          HEAP_UNPIN_ARGS(k, pinnedArgs);

          break;
        }
      case OP_SEND_MESSAGE_WITH_OPTS:
        {
          word_t result, arity, optsArrayReg, optCount;
          int k, m;
          Pinned<struct Object> selector(oh);
          struct Object* argsArray[MAX_ARITY], *pinnedArgs[MAX_ARITY], *optsArrayInline[MAX_OPTS];
          Pinned<struct OopArray> optsArray(oh);
          result = SSA_NEXT_PARAM_SMALLINT;
          selector = SSA_NEXT_PARAM_OBJECT;
          arity = SSA_NEXT_PARAM_SMALLINT;
          optsArrayReg = SSA_NEXT_PARAM_SMALLINT;
          optsArray = (struct OopArray*)SSA_REGISTER(optsArrayReg);
          optCount = array_size(optsArray);
#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "send message with opts fp: %" PRIdPTR ", result: %" PRIdPTR " arity: %" PRIdPTR ", opts: %" PRIdPTR ", message: ", i->framePointer, result, arity, optsArrayReg);
          print_type(oh, selector);
#endif
          assert(arity <= MAX_ARITY && optCount <= MAX_OPTS);

          HEAP_READ_AND_PIN_ARGS(k, arity, argsArray, pinnedArgs);
          for (m = 0; m < optCount; m++) {
            optsArrayInline[m] = optsArray->elements[m];
            heap_pin_object(oh, optsArrayInline[m]);
          }

          send_to_through_arity_with_optionals(oh, (struct Symbol*)selector, argsArray, argsArray,
                                               arity, optsArrayInline, optCount,
                                               i->framePointer + result);
          for (m = 0; m < optCount; m++) {
            heap_unpin_object(oh, optsArrayInline[m]);
          }

          HEAP_UNPIN_ARGS(k, pinnedArgs);

          break;
        }
      case OP_SEND_WITH_OPTIONALS_INLINE:
        {
          word_t result, arity, optCount;
          int k, m;
          Pinned<struct Object> selector(oh);
          struct Object* argsArray[MAX_ARITY], *pinnedArgs[MAX_ARITY], *optsArray[MAX_OPTS], *pinnedOpts[MAX_OPTS];
          result = SSA_NEXT_PARAM_SMALLINT;
          selector = SSA_NEXT_PARAM_OBJECT;
          arity = SSA_NEXT_PARAM_SMALLINT;
          optCount = SSA_NEXT_PARAM_SMALLINT;

          assert(arity <= MAX_ARITY && optCount <= MAX_OPTS);

          HEAP_READ_AND_PIN_ARGS(k, arity, argsArray, pinnedArgs);
          HEAP_READ_AND_PIN_ARGS(m, optCount, optsArray, pinnedOpts);

          send_to_through_arity_with_optionals(oh, (struct Symbol*)selector, argsArray, argsArray,
                                               arity, optsArray, optCount,
                                               i->framePointer + result);
          HEAP_UNPIN_ARGS(k, pinnedArgs);
          HEAP_UNPIN_ARGS(m, pinnedOpts);

          break;
        }
      case OP_NEW_ARRAY_WITH:
        {
          word_t result, size, k;
          Pinned<struct OopArray> array(oh);
          result = SSA_NEXT_PARAM_SMALLINT;
          size = SSA_NEXT_PARAM_SMALLINT;

#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "new array, result: %" PRIdPTR ", size: %" PRIdPTR "\n", result, size);
#endif
          array = heap_clone_oop_array_sized(oh, get_special(oh, SPECIAL_OOP_ARRAY_PROTO), size);
          for (k = 0; k < size; k++) {
            array->elements[k] = SSA_REGISTER(SSA_NEXT_PARAM_SMALLINT);
          }
          heap_store_into(oh, (struct Object*)i->stack, (struct Object*)array);
#ifdef PRINT_DEBUG_STACK
    fprintf(stderr, "%" PRIuPTR "u: setting stack[%" PRIdPTR "] = ", instruction_counter, REG_STACK_POINTER(result)); print_object((struct Object*)array);
#endif
          ASSERT_VALID_REGISTER(result);
          SSA_REGISTER(result) = (struct Object*) array;
          break;
        }
      case OP_NEW_CLOSURE:
        {
          word_t result;
          Pinned<struct CompiledMethod> block(oh);
          Pinned<struct Closure> newClosure(oh);
          result = SSA_NEXT_PARAM_SMALLINT;
          block = (struct CompiledMethod*)SSA_NEXT_PARAM_OBJECT;
#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "new closure, result: %" PRIdPTR ", block", result);
          print_type(oh, (struct Object*)block);
#endif
          if ((struct CompiledMethod *) i->closure == i->method) {
            newClosure = (struct Closure *) heap_clone_oop_array_sized(oh, get_special(oh, SPECIAL_OOP_CLOSURE_PROTO), 1);
          } else {
            word_t inheritedSize;
            
            inheritedSize = object_array_size((struct Object *) i->closure);
            newClosure = (struct Closure *) heap_clone_oop_array_sized(oh, get_special(oh, SPECIAL_OOP_CLOSURE_PROTO), inheritedSize+1);
            copy_words_into((word_t *) i->closure->lexicalWindow, inheritedSize, (word_t*) newClosure->lexicalWindow + 1);
          }
          newClosure->lexicalWindow[0] = i->lexicalContext;
          newClosure->method = block;
          heap_store_into(oh, (struct Object*)newClosure, (struct Object*)i->lexicalContext);
#if 1
          if (object_is_free((struct Object*)block)) {
            /*fprintf(stderr, "%d\n", instruction_counter);*/
          }
#endif
          heap_store_into(oh, (struct Object*)newClosure, (struct Object*)block);
          heap_store_into(oh, (struct Object*)i->stack, (struct Object*)newClosure);
#ifdef PRINT_DEBUG_STACK
    fprintf(stderr, "%" PRIuPTR "u: setting stack[%" PRIdPTR "] = ", instruction_counter, REG_STACK_POINTER(result)); print_object((struct Object*)newClosure);
#endif
          ASSERT_VALID_REGISTER(result);
          SSA_REGISTER(result) = (struct Object*) newClosure;
          break;
        }
      case OP_LOAD_LITERAL:
        {
          word_t destReg;
          Pinned<struct Object> literal(oh);
          destReg = SSA_NEXT_PARAM_SMALLINT;
          literal = SSA_NEXT_PARAM_OBJECT;
#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "load literal into reg %" PRIdPTR ", value: ", destReg);
          print_type(oh, literal);
#endif
          heap_store_into(oh, (struct Object*)i->stack, literal);
#ifdef PRINT_DEBUG_STACK
    fprintf(stderr, "%" PRIuPTR "u: setting stack[%" PRIdPTR "] = ", instruction_counter, REG_STACK_POINTER(destReg)); print_object((struct Object*)literal);
#endif
          ASSERT_VALID_REGISTER(destReg);
          SSA_REGISTER(destReg) = literal;
          break;
        }
      case OP_RESEND_MESSAGE:
        {
          word_t resultRegister, lexicalOffset;
          resultRegister = SSA_NEXT_PARAM_SMALLINT;
          lexicalOffset = SSA_NEXT_PARAM_SMALLINT;
#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "resend message reg %" PRIdPTR ", offset: %" PRIdPTR "\n", resultRegister, lexicalOffset);
#endif
          interpreter_resend_message(oh, i, lexicalOffset, i->framePointer + resultRegister);
          break;
        }
      case OP_LOAD_VARIABLE:
        {
          word_t var;
          var = SSA_NEXT_PARAM_SMALLINT;
#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "load var %" PRIdPTR "\n", var);
#endif
          if (i->method->heapAllocate == oh->cached.true_object) {
            heap_store_into(oh, (struct Object*)i->stack, (struct Object*)i->lexicalContext);
#ifdef PRINT_DEBUG_STACK
            fprintf(stderr, "%" PRIuPTR "u: setting stack[%" PRIdPTR "] = ", instruction_counter, REG_STACK_POINTER(var)); print_object(i->lexicalContext->variables[var]);
#endif
            ASSERT_VALID_REGISTER(var);
            SSA_REGISTER(var) = i->lexicalContext->variables[var];
          }
#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "var val =");
          print_type(oh, SSA_REGISTER(var));
#endif
          /*if it's not heap allocated the register is already loaded*/
          break;
        }
      case OP_STORE_VARIABLE:
        {
          word_t var;
          var = SSA_NEXT_PARAM_SMALLINT;
#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "store var %" PRIdPTR "\n", var);
#endif
          if (i->method->heapAllocate == oh->cached.true_object) {
            heap_store_into(oh, (struct Object*)i->lexicalContext, (struct Object*)i->stack);
            i->lexicalContext->variables[var] = SSA_REGISTER(var);
          }
          /*if it's not heap allocated the register is already loaded*/
#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "var val =");
          print_type(oh, SSA_REGISTER(var));
#endif
          break;
        }
      case OP_LOAD_FREE_VARIABLE:
        {
          word_t destReg, lexOffset, varIndex;
          destReg = SSA_NEXT_PARAM_SMALLINT;
          lexOffset = SSA_NEXT_PARAM_SMALLINT;
          varIndex = SSA_NEXT_PARAM_SMALLINT;
#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "load free var to: %" PRIdPTR ", lexoffset: %" PRIdPTR ", index: %" PRIdPTR "\n", destReg, lexOffset, varIndex);
#endif
          heap_store_into(oh, (struct Object*)i->stack, (struct Object*)i->closure->lexicalWindow[lexOffset-1]);
#ifdef PRINT_DEBUG_STACK
          fprintf(stderr, "%" PRIuPTR "u: setting stack[%" PRIdPTR "] = ", instruction_counter, REG_STACK_POINTER(destReg)); print_object(i->closure->lexicalWindow[lexOffset-1]->variables[varIndex]);
#endif
          ASSERT_VALID_REGISTER(destReg);
          SSA_REGISTER(destReg) = i->closure->lexicalWindow[lexOffset-1]->variables[varIndex];

#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "var val =");
          print_type(oh, SSA_REGISTER(destReg));
#endif
          break;
        }
      case OP_STORE_FREE_VARIABLE:
        {
          word_t srcReg, lexOffset, varIndex;
          lexOffset = SSA_NEXT_PARAM_SMALLINT;
          varIndex = SSA_NEXT_PARAM_SMALLINT;
          srcReg = SSA_NEXT_PARAM_SMALLINT;
#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "store free var from: %" PRIdPTR ", lexoffset: %" PRIdPTR ", index: %" PRIdPTR "\n", srcReg, lexOffset, varIndex);
#endif
          heap_store_into(oh, (struct Object*)i->closure->lexicalWindow[lexOffset-1], (struct Object*)i->stack);
          i->closure->lexicalWindow[lexOffset-1]->variables[varIndex] = SSA_REGISTER(srcReg);

#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "var val =");
          print_type(oh, SSA_REGISTER(srcReg));
#endif
          break;
        }
      case OP_MOVE_REGISTER:
        {
          word_t destReg, srcReg;
          destReg = SSA_NEXT_PARAM_SMALLINT;
          srcReg = SSA_NEXT_PARAM_SMALLINT;

#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "move reg %" PRIdPTR ", %" PRIdPTR "\n", destReg, srcReg);
#endif
          heap_store_into(oh, (struct Object*)i->stack, SSA_REGISTER(srcReg));
#ifdef PRINT_DEBUG_STACK
          fprintf(stderr, "%" PRIuPTR "u: setting stack[%" PRIdPTR "] = ", instruction_counter, REG_STACK_POINTER(destReg)); print_object(SSA_REGISTER(srcReg));
#endif
          ASSERT_VALID_REGISTER(destReg);
          SSA_REGISTER(destReg) = SSA_REGISTER(srcReg);
          break;
        }
      case OP_IS_NIL:
        {
          word_t srcReg, resultReg;
          resultReg = SSA_NEXT_PARAM_SMALLINT;
          srcReg = SSA_NEXT_PARAM_SMALLINT;
          ASSERT_VALID_REGISTER(resultReg);
          SSA_REGISTER(resultReg) = (SSA_REGISTER(srcReg) == oh->cached.nil) ? oh->cached.true_object : oh->cached.false_object;
          break;
        }
      case OP_IS_IDENTICAL_TO:
        {
          word_t destReg, srcReg, resultReg;
          resultReg = SSA_NEXT_PARAM_SMALLINT;
          destReg = SSA_NEXT_PARAM_SMALLINT;
          srcReg = SSA_NEXT_PARAM_SMALLINT;

#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "is identical %" PRIdPTR ", %" PRIdPTR "\n", destReg, srcReg);
#endif
          ASSERT_VALID_REGISTER(resultReg);
          
          SSA_REGISTER(resultReg) = (SSA_REGISTER(destReg) == SSA_REGISTER(srcReg)) ? oh->cached.true_object : oh->cached.false_object;
#ifdef PRINT_DEBUG_STACK
          fprintf(stderr, "%" PRIuPTR "u: setting stack[%" PRIdPTR "] = ", instruction_counter, REG_STACK_POINTER(resultReg)); print_object(SSA_REGISTER(resultReg));
#endif
          break;
        }
      case OP_BRANCH_KEYED:
        {
          word_t tableReg, keyReg;
          keyReg = SSA_NEXT_PARAM_SMALLINT;
          tableReg = SSA_NEXT_PARAM_SMALLINT;

          Pinned<struct OopArray> table(oh), key(oh);

          /*assert(0);*/
#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "branch keyed: %" PRIdPTR "/%" PRIdPTR "\n", tableReg, keyReg);
#endif
          table = (struct OopArray*)SSA_REGISTER(tableReg);
          key = (struct OopArray*)SSA_REGISTER(keyReg);

          interpreter_branch_keyed(oh, i, table, key);
          break;
        }
      case OP_BRANCH_IF_TRUE:
        {
          word_t condReg, offset;
          Pinned<struct Object> val(oh);
          condReg = SSA_NEXT_PARAM_SMALLINT;
          offset = SSA_NEXT_PARAM_SMALLINT - 1;

          val = SSA_REGISTER(condReg);

#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "branch if true: %" PRIdPTR ", offset: %" PRIdPTR ", val: ", condReg, offset);
          print_type(oh, val);
#endif
          if (val == oh->cached.true_object) {
            i->codePointer = i->codePointer + offset;
          } else {
            if (val != oh->cached.false_object) {
              i->codePointer = i->codePointer - 3;
              interpreter_signal_with(oh, i, get_special(oh, SPECIAL_OOP_NOT_A_BOOLEAN), val, NULL, 0, condReg /*fixme*/);
            }
          }
          break;
        }
      case OP_BRANCH_IF_FALSE:
        {
          word_t condReg, offset;
          Pinned<struct Object> val(oh);
          condReg = SSA_NEXT_PARAM_SMALLINT;
          offset = SSA_NEXT_PARAM_SMALLINT - 1;

          val = SSA_REGISTER(condReg);

#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "branch if false: %" PRIdPTR ", offset: %" PRIdPTR ", val: ", condReg, offset);
          print_type(oh, val);
#endif
          if (val == oh->cached.false_object) {
            i->codePointer = i->codePointer + offset;
          } else {
            if (val != oh->cached.true_object) {
              i->codePointer = i->codePointer - 3;
              interpreter_signal_with(oh, i, get_special(oh, SPECIAL_OOP_NOT_A_BOOLEAN), val, NULL, 0, condReg /*fixme*/);
            }
          }
          break;
        }
      case OP_JUMP_TO:
        {
          word_t offset;
          offset = SSA_NEXT_PARAM_SMALLINT - 1;
          assert(offset < 20000 && offset > -20000);
#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "jump to offset: %" PRIdPTR "\n", offset);
#endif
          i->codePointer = i->codePointer + offset;
          
          break;
        }
      case OP_LOAD_ENVIRONMENT:
        {
          word_t next_param;
          next_param = SSA_NEXT_PARAM_SMALLINT;
#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "load environment into reg %" PRIdPTR ", value: ", next_param);
          print_type(oh, i->method->environment);
#endif
          heap_store_into(oh, (struct Object*)i->stack, (struct Object*)i->method->environment);
          ASSERT_VALID_REGISTER(next_param);
          SSA_REGISTER(next_param) = i->method->environment;
#ifdef PRINT_DEBUG_STACK
          fprintf(stderr, "%" PRIuPTR "u: setting stack[%" PRIdPTR "] = ", instruction_counter, REG_STACK_POINTER(next_param)); print_object(SSA_REGISTER(next_param));
#endif
          break;
        }
      case OP_RETURN_REGISTER:
        {
          word_t reg;
          reg = SSA_NEXT_PARAM_SMALLINT;
#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "return reg %" PRIdPTR ", value: ", reg);
          print_type(oh, SSA_REGISTER(reg));
#endif
#ifdef PRINT_DEBUG_STACK
          fprintf(stderr, "%" PRIuPTR "u: ", instruction_counter);
#endif
          ASSERT_VALID_REGISTER(reg);
          Pinned<struct Object> result(oh);
          result = SSA_REGISTER(reg);
          interpreter_return_result(oh, i, 0, result, prevPointer);
#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "in function: \n");
          print_type(oh, (struct Object*)i->method);
#endif
          break;
        }
      case OP_RESUME: /*returning the result (or lack of) from the finalizer of a prim_ensure*/
        {
          PRINTOP("op: resume\n");
          /*one for the resume and one for the function that we
            interrupted the return from to run the finalizer*/
          /*interpreter_return_result(oh, i, 0, NULL);*/
          interpreter_return_result(oh, i, 0, NULL, prevPointer);
#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "in function: \n");
          print_type(oh, (struct Object*)i->method);
#endif
          break;
        }
      case OP_RETURN_FROM:
        {
          word_t reg, offset;
          PRINTOP("op: return from\n");
          reg = SSA_NEXT_PARAM_SMALLINT;
          offset = SSA_NEXT_PARAM_SMALLINT;
#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "return result reg: %" PRIdPTR ", offset: %" PRIdPTR ", value: ", reg, offset);
          print_type(oh, SSA_REGISTER(reg));
#endif
#ifdef PRINT_DEBUG_STACK
          fprintf(stderr, "%" PRIuPTR "u: ", instruction_counter);
#endif
          ASSERT_VALID_REGISTER(reg);
          Pinned<struct Object> result(oh);
          result = SSA_REGISTER(reg);
          interpreter_return_result(oh, i, offset, result, prevPointer);
#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "in function: \n");
          print_type(oh, (struct Object*)i->method);
#endif
          break;
        }
      case OP_RETURN_VALUE:
        {
          Pinned<struct Object> obj(oh);
          PRINTOP("op: return obj\n");
          obj = SSA_NEXT_PARAM_OBJECT;
#ifdef PRINT_DEBUG_STACK
          fprintf(stderr, "%" PRIuPTR "u: ", instruction_counter);
#endif
          interpreter_return_result(oh, i, 0, obj, prevPointer);
#ifdef PRINT_DEBUG_OPCODES
          fprintf(stderr, "in function: \n");
          print_type(oh, (struct Object*)i->method);
#endif
          break;
        }
      case OP_PRIMITIVE_DO:
        {
          word_t primNum, resultReg, arity, k;
          struct Object* argsArray[MAX_ARITY], *pinnedArgs[MAX_ARITY];
          primNum = object_to_smallint(SSA_REGISTER(SSA_NEXT_PARAM_SMALLINT));
          arity = SSA_NEXT_PARAM_SMALLINT;
          resultReg = SSA_NEXT_PARAM_SMALLINT;

          assert(arity <= MAX_ARITY);
          HEAP_READ_AND_PIN_ARGS(k, arity, argsArray, pinnedArgs);

          primitives[primNum](oh, argsArray, arity, NULL, 0, i->framePointer + resultReg);
          
          HEAP_UNPIN_ARGS(k, pinnedArgs);

          break;
        }

      case OP_INLINE_PRIMITIVE_CHECK:
        {
          word_t primNum, resultReg, arity, k, jumpOffset;
          struct Object* mapArray;
          struct Object* argsArray[MAX_ARITY], *pinnedArgs[MAX_ARITY];
          resultReg = SSA_NEXT_PARAM_SMALLINT;
          mapArray = SSA_NEXT_PARAM_OBJECT;
          primNum = SSA_NEXT_PARAM_SMALLINT;
          arity = SSA_NEXT_PARAM_SMALLINT;
          jumpOffset = SSA_NEXT_PARAM_SMALLINT;

          assert(arity <= MAX_ARITY);

          HEAP_READ_AND_PIN_ARGS(k, arity, argsArray, pinnedArgs);

          
          word_t success = 0;
          if (arity == object_array_size(mapArray)) {
            success = 1;
            for (word_t k = 0; k < arity; k++) {
              if ((struct Map*) ((struct OopArray*)mapArray)->elements[k] != object_get_map(oh, argsArray[k])) {
                success = 0;
                break;
              }
            }
          }

          if (success) {
            //change the code pointer before the primitive because some primitives like prim_ensure will change stuff
            i->codePointer = i->codePointer + jumpOffset; 
            primitives[primNum](oh, argsArray, arity, NULL, 0, i->framePointer + resultReg);
          }
          
          HEAP_UNPIN_ARGS(k, pinnedArgs);

          
          break;
        }

      case OP_INLINE_METHOD_CHECK:
        {
          word_t arity, k, jumpOffset;
          struct Object* mapArray;
          struct Object* argsArray[MAX_ARITY], *pinnedArgs[MAX_ARITY];
          mapArray = SSA_NEXT_PARAM_OBJECT;
          arity = SSA_NEXT_PARAM_SMALLINT;
          jumpOffset = SSA_NEXT_PARAM_SMALLINT;

          assert(arity <= MAX_ARITY);

          HEAP_READ_AND_PIN_ARGS(k, arity, argsArray, pinnedArgs);

          
          word_t success = 0;
          if (arity == object_array_size(mapArray)) {
            success = 1;
            for (word_t k = 0; k < arity; k++) {
              if ((struct Map*) ((struct OopArray*)mapArray)->elements[k] != object_get_map(oh, argsArray[k])) {
                success = 0;
                break;
              }
            }
          }

          if (!success) {
            i->codePointer = i->codePointer + jumpOffset;
          }
          
          HEAP_UNPIN_ARGS(k, pinnedArgs);

          
          break;
        }

      case OP_APPLY_TO:
        {
          word_t resultReg, arity, k;
          Pinned<struct Object> method(oh);
          struct Object* argsArray[MAX_ARITY], *pinnedArgs[MAX_ARITY];
          method = SSA_REGISTER(SSA_NEXT_PARAM_SMALLINT);
          arity = SSA_NEXT_PARAM_SMALLINT;
          resultReg = SSA_NEXT_PARAM_SMALLINT;

          assert(arity <= MAX_ARITY);

          HEAP_READ_AND_PIN_ARGS(k, arity, argsArray, pinnedArgs);

          interpreter_apply_to_arity_with_optionals(oh, oh->cached.interpreter, method,
                                                    argsArray, arity, NULL, 0, i->framePointer + resultReg);
          
          HEAP_UNPIN_ARGS(k, pinnedArgs);
          
          break;
        }

      default:
        fprintf(stderr, "error bad opcode... %" PRIdPTR "\n", op>>1);
        assert(0);
        break;
      }
      

    }
  }/* while (interpreter_return_result(oh, oh->cached.interpreter, 0, NULL, prevPointer));*/



}

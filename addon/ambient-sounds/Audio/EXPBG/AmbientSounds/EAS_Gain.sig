AudioSignalResClass {
 Inputs {
  IOPItemInputClass {
   id 1
   name "EAS_Gain"
   children { 2 }
   value 0.35
   valueMin 0
   valueMax 1
  }
 }
 Outputs {
  IOPItemOutputClass {
   id 2
   name "Gain"
   input 1
  }
 }
 compiled IOPCompiledClass {
  visited { 5 6 }
  ins { IOPCompiledIn { data { 1 2 } } }
  outs { IOPCompiledOut { data { 0 } } }
  processed 2
  version 2
 }
}

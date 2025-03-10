; Check if compiling for x86 architecture
IFDEF RAX
    END_IF_NOT_X86 equ end
ELSE
    END_IF_NOT_X86 equ <>
ENDIF

END_IF_NOT_X86

.386
.model flat, c
.code

; thanks to pseudostripy for showing me how this is done
; and for creating a proof of concept version, making this possible

; this function allows us to get the itemLotId of an item that is being picked up
; since the itemGive function only receives the contents of the itemLot
getItemLotId proc thisPtr:ptr, arg1:ptr, arg2:ptr, baseAddress:ptr

    ; Load arguments into registers
    mov esi, thisPtr     ; thisPtr
    mov ecx, arg1        ; arg1
    mov eax, arg2        ; arg2
    mov edi, baseAddress ; baseAddress

    shld eax, ecx, 16
    mov ecx, eax
    sar ecx, 16
    sar eax, 31

    mov eax, [esi+4]
    imul ecx, ecx, 4
    mov ecx, [eax+ecx]

    lea edx, arg1
    push edx

    lea eax, [edi + 258890h]
    call eax

    mov eax, [eax+120h]

    ; skip if it's an item we dropped
    test eax,eax
    jz done

    push ebp
    mov ebp, esp
    sub esp, 200h

    mov [ebp-200h], eax

    lea ecx, [ebp-200h]
    lea eax, [edi + 2062B0h]
    call eax
    mov esi, eax
    
    mov esp, ebp
    pop ebp

    mov ecx, esi 
    lea eax, [edi + 25D1B0h]
    call eax
    test eax, eax
    jz is_chest

    mov eax, [eax+28h]
    jmp done

is_chest:
    mov ecx, esi
    lea eax, [edi + 25D1F0h]
    call eax
    test eax, eax
    jz is_enemy_drop

    mov eax, [eax+40h]
    jmp done

is_enemy_drop:
    push esi
    lea eax, [edi + 25D170h]
    call eax
    test eax, eax
    jz error

    mov eax, [eax+44h]
    mov eax, [eax+80h]

    jmp done

error:
    mov eax, -1
    jmp done

done:
    ret

getItemLotId endp
end
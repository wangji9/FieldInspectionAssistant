"""Prompt-level answer length limits. Generated text is never sliced."""
import re

def visible_length(text):
    # Count displayed characters, not Markdown syntax or whitespace.
    text=re.sub(r'!?(\[([^\]]*)\])\([^)]*\)',r'\2',text)
    text=re.sub(r'^\s*(?:#{1,6}\s+|>\s*|[-*+]\s+|\d+[.)]\s+)', '', text, flags=re.M)
    text=re.sub(r'```[^\n]*\n?|[*_`~|]', '', text)
    return len(re.sub(r'\s+', '', text))

def constrain(payload,attempt=0):
    out=dict(payload)
    limit=int(payload.get('answer_limit',0))
    if payload['mode'] not in ('text','vision') or not limit:return out
    question=payload.get('question','')
    # Aim below the upper bound, leaving room for punctuation and formatting.
    target=max(8,int(limit*(.85 if attempt==0 else .65 if attempt==1 else .45)))
    instruction=(f'直接回答用户的问题。回答的可见正文（含标点、不计空白和Markdown标记）不得超过{limit}字，'
                 f'请控制在{target}字以内。根据篇幅选择要点，完整写完句子和Markdown结构；不要解释字数限制。')
    if attempt:instruction+='上次回答超长，请重新提炼更简洁的完整答案。'
    out['question']=f'{instruction}\n\n用户问题：\n{question}\n\n再次要求：完整回答，总字数不超过{limit}字。'
    # Independent engineering guardrail, not the user's requested length.
    out['max_tokens']=4096
    return out

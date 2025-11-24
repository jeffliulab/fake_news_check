from llmproxy import LLMProxy

if __name__ == '__main__':

    client = LLMProxy()

    response = client.generate(
        model = '4o-mini',
        system = 'Answer my question in a funny manner',
        query = 'Who are the Jumbos?',
        temperature=0.0,
        lastk=0,
        session_id='GenericSession',
        rag_usage = False,
    )

    print(response)